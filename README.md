# File-System-Using-FUSE
Important Notice: Fall 2023 course project for CS416 at Rutgers University. Please follow Rutgers University Academic Integrity Policy.

## Overview
In this project, we engineered a custom file system developed to provide a hands-on understanding of file system operations. This implementation includes features such as inode and data block management, directory operations, file creation, and deletion. The file system was tested extensively to ensure reliability and efficiency.

## Features

### Core Functionalities
1. **Inode Management**:
   - `get_avail_ino()`: Finds and marks an unused inode.
   - `readi()` and `writei()`: Reads and writes inode data to and from the disk.

2. **Data Block Management**:
   - `get_avail_blkno()`: Locates and allocates an available data block.

3. **Directory Operations**:
   - `dir_find()`: Searches for files or directories in a directory.
   - `dir_add()`: Adds a new entry to a directory.
   - `dir_remove()`: Removes an entry from a directory, optimizing space by reallocating blocks if necessary.

4. **Path Translation**:
   - `get_node_by_path()`: Resolves file paths to their corresponding inodes, supporting hierarchical navigation.

### File System Initialization
- `rufs_mkfs()`: Initializes the file system, setting up the superblock, bitmaps, and root directory inode.
- `rufs_init()` and `rufs_destroy()`: Handles file system startup and cleanup, ensuring consistency between memory and disk.

### File and Directory Operations
- `rufs_mkdir()`: Creates directories.
- `rufs_rmdir()`: Removes directories if they are empty.
- `rufs_create()`: Creates new files.
- `rufs_open()`, `rufs_read()`, and `rufs_write()`: Facilitates opening, reading, and writing files.
- `rufs_unlink()`: Deletes files and releases associated resources.

### Debugging and Metrics
- Reports the total blocks used and execution time for test cases.
- Supports multiple test scenarios for performance evaluation.

## Performance Metrics
- **Test Case Results**:
  - Simple test case: 0.0283 seconds, 23 blocks used.
  - NUM_FILES=1000, ITERS=160: 2.85 seconds, 72 blocks used.

## Notes
- The file system ensures data integrity during operations and supports both direct and indirect pointers for block allocation.
- The implementation includes robust error handling for all operations.

---

**Developers**: Huzaif Mansuri, Manan Patel
