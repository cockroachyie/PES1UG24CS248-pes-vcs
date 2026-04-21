// object.c — Content-addressable object store
//
// Every piece of data (file contents, directory listings, commits) is stored
// as an "object" named by its SHA-256 hash. Objects are stored under
// .pes/objects/XX/YYYYYY... where XX is the first two hex characters of the
// hash (directory sharding).
//
// PROVIDED functions: compute_hash, object_path, object_exists, hash_to_hex, hex_to_hash
// TODO functions:     object_write, object_read

#include "pes.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <openssl/evp.h>

// ─── PROVIDED ────────────────────────────────────────────────────────────────

void hash_to_hex(const ObjectID *id, char *hex_out) {
    for (int i = 0; i < HASH_SIZE; i++) {
        sprintf(hex_out + i * 2, "%02x", id->hash[i]);
    }
    hex_out[HASH_HEX_SIZE] = '\0';
}

int hex_to_hash(const char *hex, ObjectID *id_out) {
    if (strlen(hex) < HASH_HEX_SIZE) return -1;
    for (int i = 0; i < HASH_SIZE; i++) {
        unsigned int byte;
        if (sscanf(hex + i * 2, "%2x", &byte) != 1) return -1;
        id_out->hash[i] = (uint8_t)byte;
    }
    return 0;
}

void compute_hash(const void *data, size_t len, ObjectID *id_out) {
    unsigned int hash_len;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, data, len);
    EVP_DigestFinal_ex(ctx, id_out->hash, &hash_len);
    EVP_MD_CTX_free(ctx);
}

// Get the filesystem path where an object should be stored.
// Format: .pes/objects/XX/YYYYYYYY...
// The first 2 hex chars form the shard directory; the rest is the filename.
void object_path(const ObjectID *id, char *path_out, size_t path_size) {
    char hex[HASH_HEX_SIZE + 1];
    hash_to_hex(id, hex);
    snprintf(path_out, path_size, "%s/%.2s/%s", OBJECTS_DIR, hex, hex + 2);
}

int object_exists(const ObjectID *id) {
    char path[512];
    object_path(id, path, sizeof(path));
    return access(path, F_OK) == 0;
}

// ─── IMPLEMENTED ────────────────────────────────────────────────────────────

int object_write(ObjectType type, const void *data, size_t len, ObjectID *id_out) {
    // Step 1: Build the header string
    const char *type_str;
    switch (type) {
        case OBJ_BLOB:   type_str = "blob";   break;
        case OBJ_TREE:   type_str = "tree";   break;
        case OBJ_COMMIT: type_str = "commit"; break;
        default: return -1;
    }
    
    char header[128];
    int header_len = snprintf(header, sizeof(header), "%s %zu", type_str, len);
    if (header_len < 0 || header_len >= (int)sizeof(header)) return -1;
    header_len++; // Include the null terminator
    
    // Step 2: Combine header + data in a single buffer
    size_t full_size = header_len + len;
    void *full_obj = malloc(full_size);
    if (!full_obj) return -1;
    
    memcpy(full_obj, header, header_len);
    memcpy((char *)full_obj + header_len, data, len);
    
    // Step 3: Compute SHA-256 hash of the full object
    compute_hash(full_obj, full_size, id_out);
    
    // Step 4: Check if object already exists (deduplication)
    if (object_exists(id_out)) {
        free(full_obj);
        return 0;  // Already stored, nothing to do
    }
    
    // Step 5: Build the target path and create shard directory
    char path[512];
    object_path(id_out, path, sizeof(path));
    
    // Extract shard directory (.pes/objects/XX/)
    char shard_dir[512];
    snprintf(shard_dir, sizeof(shard_dir), "%s/%.2s", OBJECTS_DIR, path + strlen(OBJECTS_DIR) + 1);
    
    // Create shard directory if it doesn't exist
    mkdir(shard_dir, 0755);
    
    // Step 6: Write to temporary file
    char temp_path[512];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp.%d", path, getpid());
    
    int fd = open(temp_path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        free(full_obj);
        return -1;
    }
    
    ssize_t written = write(fd, full_obj, full_size);
    if (written != (ssize_t)full_size) {
        close(fd);
        unlink(temp_path);
        free(full_obj);
        return -1;
    }
    
    // Step 7: fsync to ensure data reaches disk
    if (fsync(fd) < 0) {
        close(fd);
        unlink(temp_path);
        free(full_obj);
        return -1;
    }
    close(fd);
    
    // Step 8: Atomically rename temp file to final path
    if (rename(temp_path, path) < 0) {
        unlink(temp_path);
        free(full_obj);
        return -1;
    }
    
    // Step 9: fsync the shard directory to persist the rename
    int dir_fd = open(shard_dir, O_RDONLY);
    if (dir_fd >= 0) {
        fsync(dir_fd);
        close(dir_fd);
    }
    
    free(full_obj);
    return 0;
}

int object_read(const ObjectID *id, ObjectType *type_out, void **data_out, size_t *len_out) {
    // Step 1: Build the file path
    char path[512];
    object_path(id, path, sizeof(path));
    
    // Step 2: Open and read the entire file
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    
    // Get file size
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (file_size < 0) {
        fclose(f);
        return -1;
    }
    
    // Read entire file
    void *file_data = malloc(file_size);
    if (!file_data) {
        fclose(f);
        return -1;
    }
    
    if (fread(file_data, 1, file_size, f) != (size_t)file_size) {
        free(file_data);
        fclose(f);
        return -1;
    }
    fclose(f);
    
    // Step 3: Verify integrity - recompute hash
    ObjectID computed_id;
    compute_hash(file_data, file_size, &computed_id);
    
    if (memcmp(id->hash, computed_id.hash, HASH_SIZE) != 0) {
        free(file_data);
        return -1;  // Corruption detected
    }
    
    // Step 4: Parse the header to find the null terminator
    char *null_pos = memchr(file_data, '\0', file_size);
    if (!null_pos) {
        free(file_data);
        return -1;
    }
    
    // Header is from start to null_pos
    char *header = (char *)file_data;
    size_t header_len = null_pos - header + 1;  // Include the \0
    
    // Data starts after the null terminator
    void *obj_data = null_pos + 1;
    size_t obj_len = file_size - header_len;
    
    // Step 5: Parse type from header
    if (strncmp(header, "blob ", 5) == 0) {
        *type_out = OBJ_BLOB;
    } else if (strncmp(header, "tree ", 5) == 0) {
        *type_out = OBJ_TREE;
    } else if (strncmp(header, "commit ", 7) == 0) {
        *type_out = OBJ_COMMIT;
    } else {
        free(file_data);
        return -1;
    }
    
    // Step 6: Allocate and copy the data portion
    *data_out = malloc(obj_len);
    if (!*data_out) {
        free(file_data);
        return -1;
    }
    
    memcpy(*data_out, obj_data, obj_len);
    *len_out = obj_len;
    
    free(file_data);
    return 0;
}
