vivynk@vivynkrishnaa:~/PES1UG24CS248-pes-vcs$ make all
./test_sequence.sh
gcc -Wall -Wextra -O2 -c commit.c -o commit.o
gcc -o pes object.o tree.o index.o commit.o pes.o -lcrypto
gcc -Wall -Wextra -O2 -c test_objects.c -o test_objects.o
gcc -o test_objects test_objects.o object.o -lcrypto
gcc -Wall -Wextra -O2 -c test_tree.c -o test_tree.o
gcc -o test_tree test_tree.o object.o tree.o -lcrypto
/usr/bin/ld: tree.o: in function `tree_from_index':
tree.c:(.text+0x743): undefined reference to `index_load'
collect2: error: ld returned 1 exit status
make: *** [Makefile:22: test_tree] Error 1
=== PES-VCS Integration Test ===

--- Repository Initialization ---
Initialized empty PES repository in .pes/
PASS: .pes/objects exists
PASS: .pes/refs/heads exists
PASS: .pes/HEAD exists

--- Staging Files ---
Status after add:
Staged changes:
  staged:     file.txt
  staged:     hello.txt

Unstaged changes:
  modified:   file.txt
  modified:   hello.txt

Untracked files:
  (nothing to show)


--- First Commit ---
Debug: Starting commit_create...
Debug: Tree created successfully.
Debug: Initial commit (no parent).
Debug: Metadata populated.
Debug: Calling commit_serialize...
Debug: Calling object_write...
Debug: Calling head_update...
Committed: facc280e1fe4... Initial commit

Log after first commit:
commit facc280e1fe4ebc1bedfbca897767de4dd442c6a131716e0a7f07c3eefed28d3
Author: PES User <pes@localhost>
Date:   1776793325

    Initial commit


--- Second Commit ---
Debug: Starting commit_create...
Debug: Tree created successfully.
Debug: Parent commit found.
Debug: Metadata populated.
Debug: Calling commit_serialize...
Debug: Calling object_write...
Debug: Calling head_update...
Committed: 7f199e69f7c9... Update file.txt

--- Third Commit ---
Debug: Starting commit_create...
Debug: Tree created successfully.
Debug: Parent commit found.
Debug: Metadata populated.
Debug: Calling commit_serialize...
Debug: Calling object_write...
Debug: Calling head_update...
Committed: ce8f28705f2d... Add farewell

--- Full History ---
commit ce8f28705f2dc3ea505f533d93131e7b1f636aa4f3427742d982e1e1d0666b07
Author: PES User <pes@localhost>
Date:   1776793325

    Add farewell

commit 7f199e69f7c9376f643ce3a6e1228c08c0eab7d93c19068766baa6650d85afe3
Author: PES User <pes@localhost>
Date:   1776793325

    Update file.txt

commit facc280e1fe4ebc1bedfbca897767de4dd442c6a131716e0a7f07c3eefed28d3
Author: PES User <pes@localhost>
Date:   1776793325

    Initial committial commiQ


--- Reference Chain ---
HEAD:
ref: refs/heads/main
refs/heads/main:
ce8f28705f2dc3ea505f533d93131e7b1f636aa4f3427742d982e1e1d0666b07

--- Object Store ---
Objects created:
10
.pes/objects/0b/d69098bd9b9cc5934a610ab65da429b525361147faa7b5b922919e9a23143d
.pes/objects/10/1f28a12274233d119fd3c8d7c7216054ddb5605f3bae21c6fb6ee3c4c7cbfa
.pes/objects/58/a67ed1c161a4e89a110968310fe31e39920ef68d4c7c7e0d6695797533f50d
.pes/objects/7f/199e69f7c9376f643ce3a6e1228c08c0eab7d93c19068766baa6650d85afe3
.pes/objects/ab/5824a9ec1ef505b5480b3e37cd50d9c80be55012cabe0ca572dbf959788299
.pes/objects/b1/7b838c5951aa88c09635c5895ef7e08f7fa1974d901ce282f30e08de0ccd92
.pes/objects/ce/8f28705f2dc3ea505f533d93131e7b1f636aa4f3427742d982e1e1d0666b07
.pes/objects/d0/7733c25d2d137b7574be8c5542b562bf48bafeaa3829f61f75b8d10d5350f9
.pes/objects/db/07a1451ca9544dbf66d769b505377d765efae7adc6b97b75cc9d2b3b3da6ff
.pes/objects/fa/cc280e1fe4ebc1bedfbca897767de4dd442c6a131716e0a7f07c3eefed28d3

=== All integration tests completed ===
