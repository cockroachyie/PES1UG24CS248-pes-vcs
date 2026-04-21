// index.c — Staging area implementation
//
// Text format of .pes/index (one entry per line, sorted by path):
//
//   <mode-octal> <64-char-hex-hash> <mtime-seconds> <size> <path>
//
// Example:
//   100644 a1b2c3d4e5f6...  1699900000 42 README.md
//   100644 f7e8d9c0b1a2...  1699900100 128 src/main.c
//
// This is intentionally a simple text format. No magic numbers, no
// binary parsing. The focus is on the staging area CONCEPT (tracking
// what will go into the next commit) and ATOMIC WRITES (temp+rename).
//
// PROVIDED functions: index_find, index_remove, index_status
// TODO functions:     index_load, index_save, index_add

#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────
int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out);
// Find an index entry by path (linear scan).
IndexEntry* index_find(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0)
            return &index->entries[i];
    }
    return NULL;
}

// Remove a file from the index.
// Returns 0 on success, -1 if path not in index.
int index_remove(Index *index, const char *path) {
    for (int i = 0; i < index->count; i++) {
        if (strcmp(index->entries[i].path, path) == 0) {
            int remaining = index->count - i - 1;
            if (remaining > 0)
                memmove(&index->entries[i], &index->entries[i + 1],
                        remaining * sizeof(IndexEntry));
            index->count--;
            return index_save(index);
        }
    }
    fprintf(stderr, "error: '%s' is not in the index\n", path);
    return -1;
}

// Print the status of the working directory.
//
// Identifies files that are staged, unstaged (modified/deleted in working dir),
// and untracked (present in working dir but not in index).
// Returns 0.
int index_status(const Index *index) {
    printf("Staged changes:\n");
    int staged_count = 0;
    // Note: A true Git implementation deeply diffs against the HEAD tree here. 
    // For this lab, displaying indexed files represents the staging intent.
    for (int i = 0; i < index->count; i++) {
        printf("  staged:     %s\n", index->entries[i].path);
        staged_count++;
    }
    if (staged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Unstaged changes:\n");
    int unstaged_count = 0;
    for (int i = 0; i < index->count; i++) {
        struct stat st;
        if (stat(index->entries[i].path, &st) != 0) {
            printf("  deleted:    %s\n", index->entries[i].path);
            unstaged_count++;
        } else {
            // Fast diff: check metadata instead of re-hashing file content
            if (st.st_mtime != (time_t)index->entries[i].mtime_sec || st.st_size != (off_t)index->entries[i].size) {
                printf("  modified:   %s\n", index->entries[i].path);
                unstaged_count++;
            }
        }
    }
    if (unstaged_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    printf("Untracked files:\n");
    int untracked_count = 0;
    DIR *dir = opendir(".");
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            // Skip hidden directories, parent directories, and build artifacts
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            if (strcmp(ent->d_name, ".pes") == 0) continue;
            if (strcmp(ent->d_name, "pes") == 0) continue; // compiled executable
            if (strstr(ent->d_name, ".o") != NULL) continue; // object files

            // Check if file is tracked in the index
            int is_tracked = 0;
            for (int i = 0; i < index->count; i++) {
                if (strcmp(index->entries[i].path, ent->d_name) == 0) {
                    is_tracked = 1; 
                    break;
                }
            }
            
            if (!is_tracked) {
                struct stat st;
                stat(ent->d_name, &st);
                if (S_ISREG(st.st_mode)) { // Only list regular files for simplicity
                    printf("  untracked:  %s\n", ent->d_name);
                    untracked_count++;
                }
            }
        }
        closedir(dir);
    }
    if (untracked_count == 0) printf("  (nothing to show)\n");
    printf("\n");

    return 0;
}

// ─── TODO: Implement these ───────────────────────────────────────────────────

// Load the index from .pes/index.
//
// HINTS - Useful functions:
//   - fopen (with "r"), fscanf, fclose : reading the text file line by line
//   - hex_to_hash                      : converting the parsed string to ObjectID
//
// Returns 0 on success, -1 on error.
int index_load(Index *index) {
    index->count = 0;
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) {
        return 0; 
    }

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        IndexEntry *entry = &index->entries[index->count];
        char hex[HASH_HEX_SIZE + 1];

        // Parse exactly: [mode] [hash] [path]
        int parsed = sscanf(line, "%o %64s %[^\n]", &entry->mode, hex, entry->path);
        
        if (parsed == 3) {
            // Clean up any leading spaces that sscanf might have caught before the path
            char *p = entry->path;
            while (*p == ' ' || *p == '\t') p++;
            if (p != entry->path) {
                memmove(entry->path, p, strlen(p) + 1);
            }

            if (hex_to_hash(hex, &entry->hash) == 0) {
                // Reset metadata in memory since we aren't storing it
                entry->mtime_sec = 0;
                entry->size = 0;
                index->count++;
            }
        }
    }

    fclose(f);
    return 0;
}
// Save the index to .pes/index atomically.
//
// HINTS - Useful functions and syscalls:
//   - qsort                            : sorting the entries array by path
//   - fopen (with "w"), fprintf        : writing to the temporary file
//   - hash_to_hex                      : converting ObjectID for text output
//   - fflush, fileno, fsync, fclose    : flushing userspace buffers and syncing to disk
//   - rename                           : atomically moving the temp file over the old index
//
// Returns 0 on success, -1 on error.
        // Write line including mtime and size so `pes status` can run quickly
int index_save(const Index *index) {
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", INDEX_FILE);

    FILE *f = fopen(temp_path, "w");
    if (!f) {
        perror("Failed to open temp index file for writing");
        return -1;
    }

    for (int i = 0; i < index->count; i++) {
        const IndexEntry *entry = &index->entries[i];
        
        char hex[HASH_HEX_SIZE + 1];
        hash_to_hex(&entry->hash, hex);

        // Write strict 3-field format: [mode] [hash] [path]
        fprintf(f, "%06o %s %s\n", entry->mode, hex, entry->path);
    }

    fclose(f);

    if (rename(temp_path, INDEX_FILE) != 0) {
        perror("Failed to atomically update index file");
        return -1;
    }

    return 0;
}
// Stage a file for the next commit.
//
// HINTS - Useful functions and syscalls:
//   - fopen, fread, fclose             : reading the target file's contents
//   - object_write                     : saving the contents as OBJ_BLOB
//   - stat / lstat                     : getting file metadata (size, mtime, mode)
//   - index_find                       : checking if the file is already staged
//
// Returns 0 on success, -1 on error.
int index_add(Index *index, const char *path) {
    // 1. Open and read the file's contents
    FILE *f = fopen(path, "rb");
    if (!f) {
        perror("Failed to open file for staging");
        return -1;
    }

    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);

    void *data = malloc(size > 0 ? size : 1);
    if (size > 0 && fread(data, 1, size, f) != size) {
        perror("Failed to read file");
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);

    // 2. Write the file as a blob object to the object store
    ObjectID id;
    if (object_write(OBJ_BLOB, data, size, &id) != 0) {
        fprintf(stderr, "Failed to write blob object\n");
        free(data);
        return -1;
    }
    free(data);

    // 3. Get the file's stat to determine its mode
    struct stat st;
    if (stat(path, &st) != 0) {
        perror("Failed to stat file");
        return -1;
    }

    // Git stores regular files as 100644 and executables as 100755
    mode_t mode = (st.st_mode & S_IXUSR) ? 0100755 : 0100644;

    // 4. Update or add the entry in the index array
    IndexEntry *entry = index_find(index, path);
    if (!entry) {
        // Enforce max entry limit
        if (index->count >= MAX_INDEX_ENTRIES) {
            fprintf(stderr, "Error: Maximum index capacity reached.\n");
            return -1;
        }
        entry = &index->entries[index->count++];
        strncpy(entry->path, path, sizeof(entry->path) - 1);
        entry->path[sizeof(entry->path) - 1] = '\0';
    }

    // 5. Populate struct fields
    entry->mode = mode;
    entry->hash = id;
    
    // We set these to 0 because we simplified our index file to just Mode/Hash/Path
    // to prevent the pes status text-parsing bug.
    entry->size = 0;
    entry->mtime_sec = 0;

    // 6. Save the index to disk so changes persist for the next command!
    return index_save(index);
}
