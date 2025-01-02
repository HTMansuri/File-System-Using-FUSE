/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */
/*

Name: Huzaif, Manan
NetId: htm23, mp1885

*/
#define FUSE_USE_VERSION 26
#define NUL '\0'

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <math.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];


// Declare your in-memory data structures here
struct superblock *my_super_block;
int num_free_blocks;
unsigned char *inode_bitmap;
unsigned char *data_bitmap;
int inode_bitmap_len;
int data_bitmap_len;
void *data_blk;
void *data_blk2;
void *data_blk3;
int debugOuter = 0;
int debugInner = 0;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	// Alread Read at INIT

	// Step 2: Traverse inode bitmap to find an available slot
	int bit;
	int index = 0;
	do{
		bit = get_bitmap(inode_bitmap, index);
		if(bit == 0){
			//set bit as used
			set_bitmap(inode_bitmap, index);
		}
		index++;
	}while(bit != 0 && index-1 < MAX_INUM);

	if(index-1 >= MAX_INUM){
		perror("No more blocks available for inode");
		return -1;
	}

	// Step 3: Update inode bitmap and write to disk
	// Write Handled in rufs_destroy

	return index-1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	
	// Step 1: Read data block bitmap from disk
	// Alread Read at INIT

	// Step 2: Traverse data block bitmap to find an available slot
	int bit;
	int index = 0;
	do{
		bit = get_bitmap(data_bitmap, index);
		if(bit == 0){
			//set bit as used
			set_bitmap(data_bitmap, index);
		}
		index++;
	}while(bit != 0 && (my_super_block->d_start_blk + index - 1) < MAX_DNUM);

	if(my_super_block->d_start_blk + index - 1 >= MAX_DNUM){
		perror("No more blocks available for data");
		return -1;
	}
	
	// Step 3: Update data block bitmap and write to disk
	// Write Handled in rufs_destroy

	return my_super_block->d_start_blk + index - 1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

	// Step 1: Get the inode's on-disk block number
	int i_blk_num = (my_super_block->i_start_blk) + ino/(BLOCK_SIZE/sizeof(struct inode));

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = (ino % (BLOCK_SIZE/sizeof(struct inode)))*sizeof(struct inode);

	// Step 3: Read the block from disk and then copy into inode structure
	memset(data_blk, 0, BLOCK_SIZE);
	bio_read(i_blk_num, data_blk);
	memcpy(inode, ((char*)data_blk + offset), sizeof(struct inode));
	
	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	int i_blk_num = (my_super_block->i_start_blk) + ino/(BLOCK_SIZE/sizeof(struct inode));

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = (ino % (BLOCK_SIZE/sizeof(struct inode)))*sizeof(struct inode);

	// Step 3: Write inode to disk 
	memset(data_blk, 0, BLOCK_SIZE);
	bio_read(i_blk_num, data_blk);
	memcpy(((char*)(data_blk + offset)), inode, sizeof(struct inode));
	bio_write(i_blk_num, data_blk);
	
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

	if(debugOuter)
		printf("\n---> ENTERING dir_find to find %s in parent_dir inode # %d", fname, ino);

  	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode dir_inode;
	readi(ino, &dir_inode);

	// Step 2: Get data block of current directory from inode
	int d_blk_num = 0;	
	int size_read = 0;
	int num_dirents = dir_inode.size/sizeof(struct dirent);
	if(debugInner)
		printf("\n     -> Num Dirents in parent_dir is %d", num_dirents);
	// Step 3: Read directory's data block and check each directory entry.
	//If the name matches, then copy directory entry to dirent structure
	while(dir_inode.direct_ptr[d_blk_num] != -1 && d_blk_num < 16 && size_read < dir_inode.size){
		int num_dirents_blk = (num_dirents > (BLOCK_SIZE/sizeof(struct dirent))) ? (BLOCK_SIZE/sizeof(struct dirent)) : num_dirents;
		if(debugInner)
			printf("\n     -> Num Dirents in block # %d is %d", dir_inode.direct_ptr[d_blk_num], num_dirents_blk);
		memset(data_blk, 0, BLOCK_SIZE);
		if(bio_read(dir_inode.direct_ptr[d_blk_num], data_blk) < 0){
			if(debugOuter)
				printf("\n---> EXITING dir_find with status FAILURE\n");
			return -EIO;
		}
		struct dirent *dirents = data_blk;
		for(int i=0; i<num_dirents_blk; i++){
			if(strncmp(dirents[i].name, fname, name_len) == 0 && dirents[i].len == (int)name_len)
			{
				memcpy(dirent, &dirents[i], sizeof(struct dirent));

				//Update accesstime in dir_inode
				time_t current_time = time(NULL);
				dir_inode.vstat.st_atime = current_time;
				writei(ino, &dir_inode);
				if(debugInner)
					printf("\n    -> SUCCESSFULLY FOUND THE ENTRY NAME %s\n",fname);
				if(debugOuter)
					printf("\n---> EXITING dir_find with status SUCCESS\n");
				return 0;
			}
		}
		num_dirents -= num_dirents_blk;
		size_read += num_dirents_blk*sizeof(struct dirent);
		d_blk_num++;
	}
	while(dir_inode.indirect_ptr[d_blk_num-16] != -1 && d_blk_num >= 16 && size_read < dir_inode.size){
		memset(data_blk, 0, BLOCK_SIZE);
		if(bio_read(dir_inode.indirect_ptr[d_blk_num-16], data_blk) < 0){
			if(debugOuter)
				printf("\n---> EXITING dir_find with status FAILURE\n");
			return -EIO;
		}
		int *ind_blk_nums = (int *)data_blk;
		int k=0;
		while(d_blk_num >= 16 && size_read < dir_inode.size){
			if(debugInner){
				printf("\n     -> Block for k = %d is %d", k, ind_blk_nums[k]);
				fflush(stdout);
			}
		int num_dirents_blk = (num_dirents > (BLOCK_SIZE/sizeof(struct dirent))) ? (BLOCK_SIZE/sizeof(struct dirent)) : num_dirents;
		if(debugInner)
			printf("\n     -> Num Dirents in block # %d is %d", dir_inode.direct_ptr[d_blk_num], num_dirents_blk);
		memset(data_blk2, 0, BLOCK_SIZE);
		if(bio_read(ind_blk_nums[k], data_blk2) < 0){
			if(debugOuter)
				printf("\n---> EXITING dir_find with status FAILURE\n");
			return -EIO;
		}

		struct dirent *dirents = data_blk2;
		for(int i=0; i<num_dirents_blk; i++){
			if(strncmp(dirents[i].name, fname, name_len) == 0 && dirents[i].len == (int)name_len)
			{
				memcpy(dirent, &dirents[i], sizeof(struct dirent));

				//Update accesstime in dir_inode
				time_t current_time = time(NULL);
				dir_inode.vstat.st_atime = current_time;
				writei(ino, &dir_inode);
				if(debugInner)
					printf("\n    -> SUCCESSFULLY FOUND THE ENTRY NAME %s\n",fname);
				if(debugOuter)
					printf("\n---> EXITING dir_find with status SUCCESS\n");
				return 0;
			}
		}
		num_dirents -= num_dirents_blk;
		size_read += num_dirents_blk*sizeof(struct dirent);
		k++;
	}
	d_blk_num++;
	}
	if(debugOuter)
		printf("\n---> EXITING dir_find with status FAILURE\n");
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
	if(debugOuter)
		printf("\n---> ENTERING dir_add to add %s in parent_dir inode # %d", fname, dir_inode.ino);
	
	struct dirent entry;
	if(dir_inode.direct_ptr[0] != -1 && dir_find(dir_inode.ino, fname, name_len, &entry) == 0){
		if(debugInner)
			printf("\n     -> File with name %s already exists", fname);
		if(debugOuter)
				printf("\n---> EXITING dir_add with status FAILURE\n");
		return -EEXIST;
	}

	// Step 3: Add directory entry in dir_inode's data block and write to disk
	entry.ino = f_ino;
	entry.len = name_len;
	strncpy(entry.name, fname, name_len);
	entry.name[name_len] = '\0';
	entry.valid = 1;
	
	int d_blk_num = (dir_inode.size/sizeof(struct dirent))/(BLOCK_SIZE/sizeof(struct dirent));
	int ind_blk_num = 0;
	int ind_blk_offset = 0;
	int offset = ((dir_inode.size/sizeof(struct dirent)) % (BLOCK_SIZE/sizeof(struct dirent)))*sizeof(struct dirent);
	// Allocate a new data block for this directory if it does not exist
	if(d_blk_num < 16 && dir_inode.direct_ptr[d_blk_num] == -1){
		offset = 0;
		int blk_num = get_avail_blkno();
		if(blk_num == -1){
			return -ENOMEM;
		}
		dir_inode.direct_ptr[d_blk_num] = blk_num;
		if(debugInner)
			printf("\n     -> New Data Block numbered %d allocated for Inode # %d", blk_num, dir_inode.ino);
		writei(dir_inode.ino, &dir_inode);
	}
	else if(d_blk_num >= 16){
		ind_blk_num = (d_blk_num-16) / (BLOCK_SIZE/sizeof(int));
		ind_blk_offset = ((d_blk_num-16) % (BLOCK_SIZE/sizeof(int)))*sizeof(int);
		if(dir_inode.indirect_ptr[ind_blk_num] == -1){
			int ptr_blk_num = get_avail_blkno();
			if(ptr_blk_num == -1){
				return -ENOMEM;
			}
			dir_inode.indirect_ptr[ind_blk_num] = ptr_blk_num;
			memset(data_blk, -1, BLOCK_SIZE);
			bio_write(ptr_blk_num, data_blk);
			writei(dir_inode.ino, &dir_inode);
		}
		if(offset == 0){
			memset(data_blk, 0, BLOCK_SIZE);
			bio_read(dir_inode.indirect_ptr[ind_blk_num], data_blk);
			int new_block = get_avail_blkno();
			if(new_block == -1){
				return -ENOMEM;
			}
			memcpy(data_blk + ind_blk_offset, &new_block, sizeof(int));
			bio_write(dir_inode.indirect_ptr[ind_blk_num], data_blk);
		}
	}
	struct dirent *dirent_addr;
	int final_blk_num;
	if(d_blk_num < 16){
		memset(data_blk, 0, BLOCK_SIZE);
		final_blk_num = dir_inode.direct_ptr[d_blk_num];
		bio_read(final_blk_num, data_blk);
		dirent_addr = data_blk + offset;
	}
	else
	{
		memset(data_blk, 0, BLOCK_SIZE);
		bio_read(dir_inode.indirect_ptr[ind_blk_num], data_blk);
		void *blk_num_addr = data_blk + ind_blk_offset;
		memcpy(&final_blk_num, blk_num_addr, sizeof(int));
		memset(data_blk, 0, BLOCK_SIZE);
		bio_read(final_blk_num, data_blk);
		dirent_addr = data_blk + offset;
	}
	memcpy(dirent_addr, &entry, sizeof(struct dirent));
	if(debugInner)
		printf("\n     -> Dirent for inode # %d of %s added to data_block # %d", f_ino, fname, final_blk_num);
	bio_write(final_blk_num, data_blk);
	
	if(debugInner){
		memset(data_blk, 0, BLOCK_SIZE);
		bio_read(final_blk_num, data_blk);
		struct dirent x;
		memcpy(&x, data_blk + offset, sizeof(struct dirent));
		printf("\n     -> the recent added enrty's name is %s with len %d\n", x.name, x.len);
	}
	
	// Update directory inode
	dir_inode.size += sizeof(struct dirent);
	dir_inode.vstat.st_size = dir_inode.size;
	
	time_t current_time = time(NULL);
	dir_inode.vstat.st_atime = current_time;
	dir_inode.vstat.st_mtime = current_time;

	// Write directory entry
	writei(dir_inode.ino, &dir_inode);
	if(debugInner)
		printf("\n     -> Inode for parent_dir with inode # %d Updated atime and mtime", dir_inode.ino);
	
	if(debugOuter)
		printf("\n---> EXITING dir_add with status SUCCESS\n");
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	// Step 2: Check if fname exist
	// Step 3: If exist, then remove it from dir_inode's data block and write to disk
	if(debugOuter)
		printf("\n---> ENTERING dir_remove to remove %s with len %d from parent_dir inode # %d", fname, (int)name_len, dir_inode.ino);
		
  	struct dirent* dirent_rem_addr;
	int dirent_rem_blk = -1;
	
	int d_blk_num = 0;	
	int size_read = 0;
	int num_dirents = dir_inode.size/sizeof(struct dirent);
	if(debugInner)
		printf("\n     -> Num Dirents in parent_dir is %d", num_dirents);
	// Step 4: Read directory's data block and check each directory entry.
	// If the name matches, then update dirent_rem_blk and dirent_rem_addr and break
	while(dirent_rem_blk == -1 && dir_inode.direct_ptr[d_blk_num] != -1 && d_blk_num < 16 && size_read < dir_inode.size){
		int num_dirents_blk = (num_dirents > (BLOCK_SIZE/sizeof(struct dirent))) ? (BLOCK_SIZE/sizeof(struct dirent)) : num_dirents;
		if(debugInner)
			printf("\n     -> Num Dirents in block # %d is %d", dir_inode.direct_ptr[d_blk_num], num_dirents_blk);
		memset(data_blk, 0, BLOCK_SIZE);
		if(bio_read(dir_inode.direct_ptr[d_blk_num], data_blk) < 0){
			if(debugOuter)
				printf("\n---> 1 EXITING dir_find with status FAILURE\n");
			return -EIO;
		}
		struct dirent *dirents = data_blk;
		for(int i=0; i<num_dirents_blk; i++){
			if(strncmp(dirents[i].name, fname, name_len) == 0 && dirents[i].len == (int)name_len)
			{
				dirent_rem_addr = &dirents[i];
				dirent_rem_blk = dir_inode.direct_ptr[d_blk_num];
				dir_inode.size -= sizeof(struct dirent);
				
				// First dirent in block & the only dirent in block
				if(i == 0 && num_dirents_blk == 1){
					// Deallocate this block
					memset(&dirents[i], 0, sizeof(struct dirent));
					bio_write(dir_inode.direct_ptr[d_blk_num], data_blk);
					unset_bitmap(data_bitmap, dir_inode.direct_ptr[d_blk_num] - my_super_block->d_start_blk);
					dir_inode.direct_ptr[d_blk_num] = -1;
					
					// Update accesstime in dir_inode
					time_t current_time = time(NULL);
					dir_inode.vstat.st_atime = current_time;
				
					writei(dir_inode.ino, &dir_inode);
					return 0;
				}
				break;
			}
		}
		num_dirents -= num_dirents_blk;
		size_read += num_dirents_blk*sizeof(struct dirent);
		d_blk_num++;
	}
	while(dirent_rem_blk == -1 && d_blk_num >= 16 && dir_inode.indirect_ptr[d_blk_num-16] != -1 && size_read < dir_inode.size){
		memset(data_blk2, 0, BLOCK_SIZE);
		if(bio_read(dir_inode.indirect_ptr[d_blk_num-16], data_blk2) < 0){
			if(debugOuter)
				printf("\n---> 2 EXITING dir_find with status FAILURE\n");
			return -EIO;
		}
		int *ind_blk_nums = (int *)data_blk2;
		int k=0;
		while(d_blk_num >= 16 && size_read < dir_inode.size){
		int num_dirents_blk = (num_dirents > (BLOCK_SIZE/sizeof(struct dirent))) ? (BLOCK_SIZE/sizeof(struct dirent)) : num_dirents;
		if(debugInner)
			printf("\n     -> Num Dirents in block # %d is %d", dir_inode.direct_ptr[d_blk_num], num_dirents_blk);
		memset(data_blk, 0, BLOCK_SIZE);
		if(bio_read(ind_blk_nums[k], data_blk) < 0){
			if(debugOuter)
				printf("\n---> 3 EXITING dir_find with status FAILURE\n");
			return -EIO;
		}

		struct dirent *dirents = data_blk;
		for(int i=0; i<num_dirents_blk; i++){
			if(strncmp(dirents[i].name, fname, name_len) == 0 && dirents[i].len == (int)name_len)
			{
				dirent_rem_addr = &dirents[i];
				dirent_rem_blk = ind_blk_nums[k];
				dir_inode.size -= sizeof(struct dirent);
				//Write code to deallocate block if last and first entry in block
				if(i==0 && num_dirents_blk == 1){
					//deallocate this block
					memset(&dirents[i], 0, sizeof(struct dirent));
					bio_write(dirent_rem_blk, data_blk);
					unset_bitmap(data_bitmap, ind_blk_nums[k] - my_super_block->d_start_blk);
					memset(&ind_blk_nums[k], -1, sizeof(int));
					bio_write(dir_inode.indirect_ptr[d_blk_num-16], data_blk2);
					if(k == 0){
						unset_bitmap(data_bitmap, dir_inode.indirect_ptr[d_blk_num-16] - my_super_block->d_start_blk);
						dir_inode.indirect_ptr[d_blk_num-16] = -1;
					}
					
					//Update accesstime in dir_inode
					time_t current_time = time(NULL);
					dir_inode.vstat.st_atime = current_time;
				
					writei(dir_inode.ino, &dir_inode);
					return 0;	
				}
				break;
			}
		}
		num_dirents -= num_dirents_blk;
		size_read += num_dirents_blk*sizeof(struct dirent);
		k++;
	}
	d_blk_num++;
	}

	if(dirent_rem_blk == -1){
		if(debugOuter)
				printf("\n---> 1 EXITING dir_find with status FAILURE\n");
		return -1;
	}
	
	d_blk_num = (dir_inode.size/sizeof(struct dirent))/(BLOCK_SIZE/sizeof(struct dirent));
	int ind_blk_num = 0;
	int ind_blk_offset = 0;
	int offset = ((dir_inode.size/sizeof(struct dirent)) % (BLOCK_SIZE/sizeof(struct dirent)))*sizeof(struct dirent);
	struct dirent *last_dirent_addr;
	int final_blk_num;
	if(d_blk_num < 16){
		if(dir_inode.direct_ptr[d_blk_num] == -1)
			return -1;
		final_blk_num = dir_inode.direct_ptr[d_blk_num];
	}
	else if(d_blk_num >= 16){
		ind_blk_num = (d_blk_num-16) / (BLOCK_SIZE/sizeof(int));
		ind_blk_offset = ((d_blk_num-16) % (BLOCK_SIZE/sizeof(int)))*sizeof(int);
		if(dir_inode.indirect_ptr[ind_blk_num] == -1)
			return -1;
		memset(data_blk3, 0, BLOCK_SIZE);
		bio_read(dir_inode.indirect_ptr[ind_blk_num], data_blk3);
		void *blk_num_addr = data_blk3 + ind_blk_offset;
		memcpy(&final_blk_num, blk_num_addr, sizeof(int));
		if(final_blk_num == -1)
			return -1;
	}
	
	memset(data_blk3, 0, BLOCK_SIZE);
	bio_read(final_blk_num, data_blk3);
	last_dirent_addr = data_blk3 + offset;
	
	memcpy(dirent_rem_addr, last_dirent_addr, sizeof(struct dirent));

	memset(last_dirent_addr, 0, sizeof(struct dirent));
	bio_write(dirent_rem_blk, data_blk);
	if(dirent_rem_blk != final_blk_num)
		bio_write(final_blk_num, data_blk3);
	if(offset == 0 && num_dirents == 1){
		if(d_blk_num < 16){
			unset_bitmap(data_bitmap, final_blk_num - my_super_block->d_start_blk);
			dir_inode.direct_ptr[d_blk_num] = -1;
		}
		else{
			unset_bitmap(data_bitmap, final_blk_num - my_super_block->d_start_blk);
			memset(data_blk3+ind_blk_offset, -1, sizeof(int));
			bio_write(dir_inode.indirect_ptr[ind_blk_num], data_blk3);
			if(ind_blk_offset == 0){
				unset_bitmap(data_bitmap, dir_inode.indirect_ptr[ind_blk_num] - my_super_block->d_start_blk);
				dir_inode.indirect_ptr[ind_blk_num] = -1;
			}
		}
	}

	time_t current_time = time(NULL);
	dir_inode.vstat.st_atime = current_time;

	writei(dir_inode.ino, &dir_inode);
	
	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	if(debugOuter)
		printf("\n---> ENTERING get_node_by_path to find node for %s", path);
	size_t length = strlen(path);
	if(path[0] == '/' && length == 1){
		if(debugInner)
			printf("\n     -> Returned root inode");
		readi(ino, inode);
		if(debugOuter)
			printf("\n---> EXITING from get node by path with status SUCCESS\n");
		return 0;
	}
	const char *fname;
	int str_index;
	fname = path+1; 
	str_index = 1; 
	length--;
	struct dirent entry;
	int name_len = 1;
	while(length > 0){
		if(path[str_index] == '/' || length == 1){
			name_len = (length == 1) ? name_len : name_len - 1;
			if(dir_find(ino, fname, name_len, &entry)<0){
				if(debugInner)
					printf("\n     -> Entry not found for %s", fname);
				if(debugOuter)
					printf("\n---> EXITING from get_node_by_path with status FAILURE\n");
				return -1;
			}
			if(length == 1){
				readi(entry.ino, inode);
				if(debugInner)
					printf("\n     -> Inode for %s is %d\n", fname, inode->ino);
				break;
			}
			ino = entry.ino;
			fname += name_len+1;
			name_len = 1;
		}
		else
			name_len++;
		str_index++;
		length--;
	}
	if(debugOuter)
		printf("\n---> EXITING from get node by path with status SUCCESS\n");
	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	if(debugOuter)
		printf("\n ---> ENTERING rufs_mkfs");
	dev_init(diskfile_path);
	if(dev_open(diskfile_path) == 0){
		data_blk = malloc(BLOCK_SIZE);
		
		my_super_block = malloc(sizeof(struct superblock));
		
		// write superblock information
		my_super_block->i_bitmap_blk = 1;
		my_super_block->d_bitmap_blk = 2;
		my_super_block->max_inum = MAX_INUM;
		my_super_block->max_dnum = MAX_DNUM;
		my_super_block->i_start_blk = 3;
		my_super_block->magic_num = MAGIC_NUM;
		my_super_block->d_start_blk = my_super_block->i_start_blk + (MAX_INUM * sizeof(struct inode) ) / BLOCK_SIZE;
		
		memset(data_blk, 0, BLOCK_SIZE);
		memcpy(data_blk, my_super_block, sizeof(struct superblock));
		bio_write(0, my_super_block);
		

		// initialize inode bitmap
		num_free_blocks = (MAX_INUM * sizeof(struct inode) ) / BLOCK_SIZE;
		inode_bitmap_len = ((BLOCK_SIZE/sizeof(struct inode))*num_free_blocks)/8;    //1 Byte = 8 bits, so divide by 8
		inode_bitmap = malloc(inode_bitmap_len);
		int inode_bitmap_arr_len = inode_bitmap_len;
		while(inode_bitmap_arr_len > 0){
			inode_bitmap[inode_bitmap_arr_len - 1] = 0;
			inode_bitmap_arr_len--;
		}
		memset(data_blk, 0, BLOCK_SIZE);
		memcpy(data_blk, inode_bitmap, inode_bitmap_len);
		bio_write(1, data_blk);

		// initialize data block bitmap
		num_free_blocks = MAX_DNUM - num_free_blocks - 3;
		data_bitmap_len = num_free_blocks/8;    //1 Byte = 8 bits, so divide by 8
		data_bitmap = malloc(data_bitmap_len);
		int data_bitmap_arr_len = data_bitmap_len;
		while(data_bitmap_arr_len > 0){
			data_bitmap[data_bitmap_arr_len - 1] = 0;
			data_bitmap_arr_len--;
		}
		memset(data_blk, 0, BLOCK_SIZE);
		memcpy(data_blk, data_bitmap, data_bitmap_len);
		bio_write(2, data_blk);
		
		// update bitmap information for root directory
		int r_inode_bit = get_avail_ino();

		
		// update inode for root directory
		// memset(data_blk, 0, BLOCK_SIZE);
		struct inode root_inode;
		root_inode.ino = r_inode_bit;
		root_inode.size = 0;		// Update size when writing to file's data block
		root_inode.valid = 1;
		root_inode.type = __S_IFDIR;
		root_inode.link = 2;
		root_inode.vstat.st_dev = 0;
		root_inode.vstat.st_ino = root_inode.ino;
		root_inode.vstat.st_mode = __S_IFDIR | 0755;  // Directory with permissions 0755
		root_inode.vstat.st_nlink = root_inode.link;
		root_inode.vstat.st_uid = getuid();
		root_inode.vstat.st_gid = getgid();
		root_inode.vstat.st_rdev = 0;
		root_inode.vstat.st_size = root_inode.size;
		root_inode.vstat.st_blksize = BLOCK_SIZE;
		root_inode.vstat.st_blocks = 0;
		
		time_t current_time = time(NULL);
		root_inode.vstat.st_atime = current_time;
		root_inode.vstat.st_mtime = current_time;
		root_inode.vstat.st_blocks = 0;

		for(int i=0; i<16; i++)
			root_inode.direct_ptr[i] = -1;
		for(int i=0; i<8; i++)
			root_inode.indirect_ptr[i] = -1;

		memset(data_blk, 0, BLOCK_SIZE);
		memcpy(data_blk, &root_inode, sizeof(struct inode));
		bio_write(my_super_block->i_start_blk, data_blk);
		if(debugOuter)
			printf("\n---> EXITING rufs_mkfs\n");
	}

	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	if(debugOuter)
		printf("\n---> ENTERING rufs_init");
	if(dev_open(diskfile_path) == -1)
		rufs_mkfs();
	else{
		my_super_block = malloc(sizeof(struct superblock));
		data_blk = malloc(BLOCK_SIZE);
		bio_read(0, data_blk);
		memcpy(my_super_block, data_blk, sizeof(struct superblock));
		inode_bitmap = malloc(BLOCK_SIZE);
		bio_read(1, (void*)inode_bitmap);
		data_bitmap = malloc(BLOCK_SIZE);
		bio_read(2, (void*)data_bitmap);
	}
	// Step 1b: If disk file is found, just initialize in-memory data structures
	// and read superblock from disk
	data_blk2 = malloc(BLOCK_SIZE);
	data_blk3 = malloc(BLOCK_SIZE);
	if(debugOuter)
		printf("\n---> EXITING rufs_init\n");
	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	bio_write(0, (void*)my_super_block);
	bio_write(1, (void*)inode_bitmap);
	bio_write(2, (void*)data_bitmap);

	bio_read(my_super_block->d_bitmap_blk, data_bitmap);
    int numBlocksUsed = 0;
    for(int i = 0; i < MAX_DNUM; i++)
    {
        if(get_bitmap(data_bitmap, i) == 1)
        {
            numBlocksUsed++;
        }
    }
	//Printing Total Blocks used excluding the superblock Block, data_bitmap Block, and inode_bitmap Block
    printf("Num blocks used: %d\n",numBlocksUsed - 3);

	free(my_super_block);
	free(data_blk);
	free(data_blk2);
	free(data_blk3);
	free(inode_bitmap);
	free(data_bitmap);

	// Step 2: Close diskfile
	dev_close(diskfile_path);
	if(debugOuter)
		printf("\n---> EXITING rufs_destroy\n");
}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	if(debugOuter)
		printf("\n---> ENTERING rufs_getattr");
	// Step 1: call get_node_by_path() to get inode from path
	struct inode final_inode;
    if (get_node_by_path(path, 0, &final_inode) < 0) {
		if(debugOuter)
			printf("\n---> Exiting the rufs_getattr with status FAILURE\n");
        return -ENOENT;
    }
	if(debugInner)
		printf("\n     -> Final Inode's # returned from get_node is %d", final_inode.ino);
    // Step 2: fill attribute of file into stbuf from inode
    // Set mode
	stbuf->st_mode = final_inode.vstat.st_mode;

    // Set number of hard links.
    stbuf->st_nlink = final_inode.link;

    // Set size of the file
    stbuf->st_size = final_inode.size;

    // Set uid and gid
    stbuf->st_uid = final_inode.vstat.st_uid;
    stbuf->st_gid = final_inode.vstat.st_gid;

    // Set the time fields
    stbuf->st_atime = final_inode.vstat.st_atime; // Access time
    stbuf->st_mtime = final_inode.vstat.st_mtime; // Modification time
    
	if (S_ISDIR(stbuf->st_mode)) {
        stbuf->st_mode |= __S_IFDIR;
        stbuf->st_nlink = 2;  // Default for directories
    } else {
        stbuf->st_mode |= __S_IFREG;
    }
	if(debugOuter)
		printf("\n---> Exiting the rufs_getattr with status SUCCESS\n");
    return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {
	if(debugOuter)
		printf("\n---> ENTERING rufs_opendir");
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode final_inode;
	if(get_node_by_path(path, 0, &final_inode) < 0){
		// Step 2: If not find, return -1
		if(debugOuter)
			printf("\n---> Exiting the rufs_opendir with status FAILURE\n");
        return -1;
	}

	if(debugOuter)
		printf("\n---> Exiting the rufs_opendir with status SUCCESS\n");
    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode dir_inode;
	if(debugOuter)
		printf("\n---> ENTERING rufs_readdir");
	get_node_by_path(path, 0, &dir_inode);

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	int d_blk_num = 0;	
	int size_read = 0;
	int num_dirents = dir_inode.size/sizeof(struct dirent);
	if(debugInner)
		printf("\n     -> Num Dirents in parent_dir ino # %d is %d", dir_inode.ino, num_dirents);
	
	while(dir_inode.direct_ptr[d_blk_num] != -1 && d_blk_num < 16 && size_read < dir_inode.size){

		int num_dirents_blk = (num_dirents > (BLOCK_SIZE/sizeof(struct dirent))) ? (BLOCK_SIZE/sizeof(struct dirent)) : num_dirents;
		if(debugInner){
			printf("\n     -> Direct\n     -> Num Dirents in block # %d is %d", dir_inode.direct_ptr[d_blk_num], num_dirents_blk);
			fflush(stdout);
		}
		memset(data_blk, 0, BLOCK_SIZE);
		bio_read(dir_inode.direct_ptr[d_blk_num], data_blk);
		struct dirent *dirents = data_blk;
		for(int i=0; i<num_dirents_blk; i++){
			char temp_name[208];
			int name_len = strlen(dirents[i].name);
			strncpy(temp_name, dirents[i].name, name_len);
			temp_name[name_len] = '\0';
			if(filler(buffer, temp_name, NULL, offset) != 0){
				return -ENOMEM;
			}
		}
		num_dirents -= num_dirents_blk;
		size_read += num_dirents_blk*sizeof(struct dirent);
		d_blk_num++;
	}
	while(d_blk_num >= 16 && dir_inode.indirect_ptr[d_blk_num-16] != -1 && size_read < dir_inode.size){
		memset(data_blk, 0, BLOCK_SIZE);
		if(bio_read(dir_inode.indirect_ptr[d_blk_num-16], data_blk) < 0){
			if(debugOuter)
				printf("\n---> EXITING readdir with status FAILURE\n");
			debugInner = 0;
			debugOuter = 0;
			return -EIO;
		}
		int *ind_blk_nums = (int *)data_blk;
		int k = 0;
		while(d_blk_num >= 16 && size_read < dir_inode.size){
		int num_dirents_blk = (num_dirents > (BLOCK_SIZE/sizeof(struct dirent))) ? (BLOCK_SIZE/sizeof(struct dirent)) : num_dirents;
		if(debugInner)
			printf("\n     -> Num Dirents in block # %d is %d", ind_blk_nums[k], num_dirents_blk);
		memset(data_blk2, 0, BLOCK_SIZE);
		if(bio_read(ind_blk_nums[k], data_blk2) < 0){
			if(debugOuter)
				printf("\n---> EXITING readdir with status FAILURE\n");
			debugInner = 0;
			debugOuter = 0;
			return -EIO;
		}

		struct dirent *dirents = data_blk2;
		for(int i=0; i<num_dirents_blk; i++){
			char temp_name[208];
			strncpy(temp_name, dirents[i].name, dirents[i].len);
			temp_name[dirents[i].len] = '\0';
			if(filler(buffer, temp_name, NULL, offset) != 0)
			{
				debugInner = 0;
				debugOuter = 0;
				return -ENOMEM;
			}				
		}
		num_dirents -= num_dirents_blk;
		size_read += num_dirents_blk*sizeof(struct dirent);
		k++;
	}
	d_blk_num++;
	}
	printf("\n     -> SegFault Here 1");
	fflush(stdout);

	if(debugOuter)
		printf("\n---> Exiting the rufs_readdir with status SUCCESS\n");
	return 0;
}

int dir_base_split(const char *path, char *dir_name, char *base_name){

	int length = strlen(path);
    int basename_len = 0;

    // Find the length of the base name
    while (length > 0 && path[length - 1] != '/') {
        basename_len++;
        length--;
    }

    // Copy the base name and null-terminate it
    strncpy(base_name, path + length, basename_len);
    base_name[basename_len] = '\0';

    // Copy the directory name and null-terminate it
    if (length > 1) { // if path is more than just "/"
        strncpy(dir_name, path, length - 1);
        dir_name[length - 1] = '\0';
    } else {
        // strncpy(dir_name, "/", 1); // root directory
        dir_name[0] = '/';
		dir_name[1] = '\0';
    }

    return 0;
}

static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	if(debugOuter)
		printf("\n---> ENTERING rufs_mkdir");
	char *dir_name = (char *)malloc(strlen(path) + 1);
	char *base_name = (char *)malloc(strlen(path) + 1);
	if(debugInner){
		printf("Original Path given: %s", path);
		printf("Original Path Len given: %d", (int)strlen(path));
	}
	dir_base_split(path, dir_name, base_name);
	if(debugInner){
		printf("\nParent Directory: %s", dir_name);
		printf("\nTarget File: %s", base_name);
	}

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode dir_inode;
	if(debugInner)
		printf("     -> going to call get_node_by_path in mkdir\n");
	if(get_node_by_path(dir_name, 0, &dir_inode)<0){
		if(debugOuter)
			printf("\n---> Exiting the rufs_mkdir with status FAILURE\n");
		free(base_name);
		free(dir_name);
		return -ENOENT;
	}
	if(debugInner)
		printf("\n     -> Dir Inode's Ino # is %d", dir_inode.ino);
	
	struct dirent entry;
	if(dir_inode.direct_ptr[0] != -1 && dir_find(dir_inode.ino, base_name, strlen(base_name), &entry) == 0){
		if(debugInner)
			printf("\n     -> Directory/File with name %s already exists", base_name);
		if(debugOuter)
				printf("\n---> EXITING dir_add with status FAILURE\n");
		return -EEXIST;
	}

	if(debugInner)
		printf("     -> going to call get_avail_ino in mkdir\n");
	
	// Step 3: Call get_avail_ino() to get an available inode number
	int ino = get_avail_ino();
	if(ino == -1){
		return -ENOMEM;
	}
	if(debugInner)
		printf("\n     ->New inode #: %d", ino);

	if(debugInner)
		printf(" \n     -> going to call dir_add in mkdir\n");
	
	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	int ret = dir_add(dir_inode, ino, base_name, strlen(base_name));
	if(ret < 0)
	{
		if(debugOuter)
			printf("\n---> Exiting the rufs_mkdir with status FAILURE\n");
		free(base_name);
		free(dir_name);
		return ret;
	}

	struct inode f_inode;
	f_inode.ino = ino;
	f_inode.size = 0;		// Update size when writing to file's data block
	f_inode.valid = 1;
	f_inode.type = __S_IFDIR | (mode & 0777);
	f_inode.link = 2;
	f_inode.vstat.st_dev = 0;
	f_inode.vstat.st_ino = f_inode.ino;
	f_inode.vstat.st_mode = f_inode.type;  // Directory with permissions as provided
	f_inode.vstat.st_nlink = f_inode.link;
	f_inode.vstat.st_uid = getuid();
	f_inode.vstat.st_gid = getgid();
	f_inode.vstat.st_rdev = 0;
	f_inode.vstat.st_size = f_inode.size;
	f_inode.vstat.st_blksize = BLOCK_SIZE;
	f_inode.vstat.st_blocks = 0;
	
	time_t current_time = time(NULL);
	f_inode.vstat.st_atime = current_time;
	f_inode.vstat.st_mtime = current_time;

	for(int i=0; i<16; i++)
		f_inode.direct_ptr[i] = -1;
	for(int i=0; i<8; i++)
		f_inode.indirect_ptr[i] = -1;

	// Step 6: Call writei() to write inode to disk
	writei(ino, &f_inode);
	
	if(debugOuter)
		printf("\n---> Exiting the rufs_mkdir with status SUCCESS\n");
    free(base_name);
	free(dir_name);
	return 0;
}

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	if(debugOuter)
		printf("\n---> ENTERING rufs_rmdir");
	char *dir_name = (char *)malloc(strlen(path) + 1);
	char *base_name = (char *)malloc(strlen(path) + 1);;
	if(debugInner){
		printf("Original Path given: %s", path);
		printf("Original Path Len given: %d", (int)strlen(path));
	}
	dir_base_split(path, dir_name, base_name);
	if(debugInner){
		printf("\nParent Directory: %s", dir_name);
		printf("\nTarget File: %s", base_name);
	}
	
	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode final_inode;
	// Step 2: If not find, return -1
	if(get_node_by_path(path, 0, &final_inode) < 0){
		if(debugInner)
			printf("\n    -> Path not found");
		free(base_name);
		free(dir_name);
		return -1;
	}

	// Step 3: Clear data block bitmap of target directory and its data block
	// There should be no data blocks as dir should be empty
	if(final_inode.size > 0){
		if(debugInner)
            printf("\n    -> Directory is not empty");
		free(base_name);
		free(dir_name);
        return -ENOTEMPTY; // or another appropriate error code
	}

	// Clearing Data_blocks not required

	// Step 4: Clear inode bitmap
	unset_bitmap(inode_bitmap, final_inode.ino);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode dir_inode;
	if(debugInner)
		printf("     -> going to call get_node_by_path in rmdir\n");
	if(get_node_by_path(dir_name, 0, &dir_inode)<0){
		if(debugOuter)
			printf("\n---> Exiting the rufs_rmdir with status FAILURE\n");
		free(base_name);
		free(dir_name);
		return -ENOENT;
	}

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	printf("Entering dir_remove now");
	fflush(stdout);
	if(dir_remove(dir_inode, base_name, strlen(base_name)) < 0)
	{
		if(debugOuter)
			printf("\n---> Exiting the rufs_rmdir with status FAILURE\n");
		free(base_name);
		free(dir_name);
		return -EIO;
	}

	if(debugOuter)
		printf("\n---> Exiting the rufs_rmdir with status SUCCESS\n");
	free(base_name);
	free(dir_name);
	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	if(debugOuter)
		printf("\n---> ENTERING rufs_mkdir");
	char *dir_name = (char *)malloc(strlen(path) + 1);
	char *base_name = (char *)malloc(strlen(path) + 1);
	if(debugInner){
		printf("Original Path given: %s", path);
		printf("Original Path Len given: %d", (int)strlen(path));
	}
	dir_base_split(path, dir_name, base_name);
	if(debugInner){
		printf("\nParent Directory: %s", dir_name);
		printf("\nTarget File: %s", base_name);
	}
	
	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode dir_inode;
	if(debugInner)
		printf("     -> going to call get_node_by_path in mkdir\n");
	if(get_node_by_path(dir_name, 0, &dir_inode)<0){
		if(debugOuter)
			printf("\n---> Exiting the rufs_opendir with status FAILURE\n");
		free(base_name);
		free(dir_name);
		return -ENOENT;
	}
	if(debugInner)
		printf("\n     -> Dir Inode's Ino # is %d", dir_inode.ino);

	struct dirent entry;
	if(dir_inode.direct_ptr[0] != -1 && dir_find(dir_inode.ino, base_name, strlen(base_name), &entry) == 0){
		if(debugInner)
			printf("\n     -> Directory/File with name %s already exists", base_name);
		if(debugOuter)
				printf("\n---> EXITING dir_add with status FAILURE\n");
		return -EEXIST;
	}

	if(debugInner)
		printf("     -> going to call get_avail_ino in mkdir\n");
	
	// Step 3: Call get_avail_ino() to get an available inode number
	int ino = get_avail_ino();
	if(ino == -1){
		return -ENOMEM;
	}
	if(debugInner)
		printf("\n     ->New inode #: %d", ino);

	if(debugInner)
		printf(" \n     -> going to call dir_add in mkdir\n");
	
	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	if(dir_add(dir_inode, ino, base_name, strlen(base_name)) < 0)
	{
		if(debugOuter)
			printf("\n---> Exiting the rufs_opendir with status FAILURE\n");
		free(base_name);
		free(dir_name);
		return -EIO;
	}

	// Step 5: Update inode for target file
	struct inode f_inode;
	f_inode.ino = ino;
	f_inode.size = 0;		// TODO: Update size when writing to file's data block
	f_inode.valid = 1;
	f_inode.type = __S_IFREG | (mode & 0777);
	f_inode.link = 1;
	f_inode.vstat.st_dev = 0;
	f_inode.vstat.st_ino = f_inode.ino;
	f_inode.vstat.st_mode = f_inode.type;  // Directory with permissions as provided
	f_inode.vstat.st_nlink = f_inode.link;
	f_inode.vstat.st_uid = getuid();
	f_inode.vstat.st_gid = getgid();
	f_inode.vstat.st_rdev = 0;
	f_inode.vstat.st_size = f_inode.size;
	f_inode.vstat.st_blksize = BLOCK_SIZE;
	f_inode.vstat.st_blocks = 0;
	
	time_t current_time = time(NULL);
	f_inode.vstat.st_atime = current_time;
	f_inode.vstat.st_mtime = current_time;

	for(int i=0; i<16; i++)
		f_inode.direct_ptr[i] = -1;
	for(int i=0; i<8; i++)
		f_inode.indirect_ptr[i] = -1;

	// Step 6: Call writei() to write inode to disk
	writei(ino, &f_inode);
	
	if(debugOuter)
		printf("\n---> Exiting the rufs_opendir with status SUCCESS\n");
	debugInner = 0;
	free(base_name);
	free(dir_name);
    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	if(debugOuter)
		printf("\n---> ENTERING rufs_open");
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode my_inode;
	// Step 2: If not find, return -1
	if(get_node_by_path(path, 0, &my_inode) < 0){
		if(debugInner)
			printf("\n    -> Path not found");
		return -1;
	}
	if(debugOuter)
		printf("\n---> EXITING rufs_open\n");
	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    if (debugOuter)
        printf("\n---> ENTERING rufs_read");

    struct inode my_inode;
    if (get_node_by_path(path, 0, &my_inode) != 0) {
        perror("Error getting inode for the target inode");
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    int temp_size = 0;
    int blk_read_loc = (offset % BLOCK_SIZE);
    int start_blk = offset / BLOCK_SIZE;

    while (temp_size < size) {
        // Handling direct pointers
        if (start_blk < 16) {
            memset(data_blk, 0, BLOCK_SIZE);
            bio_read(my_inode.direct_ptr[start_blk], data_blk);

            // Read from block
            int limit = (size - temp_size) < (BLOCK_SIZE - blk_read_loc) ? (size - temp_size) : (BLOCK_SIZE - blk_read_loc);
            memcpy(buffer + temp_size, data_blk + blk_read_loc, limit);

            temp_size += limit;
            start_blk++;
            blk_read_loc = 0;
        } else {
            // Handling indirect pointers
            // Removing direct data blocks
            start_blk -= 16;
            start_blk /= (BLOCK_SIZE / sizeof(int));
            int start_blk_db_offset = start_blk % (BLOCK_SIZE / sizeof(int));

            // Knowing the data block index and reading it
            int final_blk_num = my_inode.indirect_ptr[start_blk];
            // If the indirect pointer is not allocated, allocate it
            if (final_blk_num == -1) {
                return -1;
            }

            memset(data_blk, 0, BLOCK_SIZE);
            bio_read(final_blk_num, data_blk);
            int *entries = (int *)data_blk;
            int db_to_read = entries[start_blk_db_offset];

            if (db_to_read == -1) {
                return -1;
            }

            memset(data_blk, 0, BLOCK_SIZE);
            bio_read(db_to_read, data_blk);

            // Read from block
            int limit = (size - temp_size) < (BLOCK_SIZE - final_blk_num) ? (size - temp_size) : (BLOCK_SIZE - final_blk_num);
            memcpy(buffer + temp_size, data_blk + final_blk_num, limit);

            temp_size += limit;
            start_blk++;
            final_blk_num = 0;
        }
    }

    // Step 4: Update the inode info and write it to disk
    time_t current_time = time(NULL);
    my_inode.vstat.st_atime = current_time;
    my_inode.vstat.st_mtime = current_time;
    writei(my_inode.ino, &my_inode);

    if (debugOuter)
        printf("\n---> EXITING rufs_read\n");

    // Note: this function should return the amount of bytes you copied to buffer
    return size;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
    if (debugOuter)
        printf("\n---> ENTERING rufs_write");

    struct inode my_inode;
    if (get_node_by_path(path, 0, &my_inode) != 0) {
        perror("Error getting inode for the target inode");
        return -ENOENT; // Return appropriate error code for "No such file or directory"
    }

    int temp_size = 0;
    int blk_write_loc = offset % BLOCK_SIZE;
    int start_blk = offset / BLOCK_SIZE;

    while (temp_size < size) {
        // Handling direct pointers
        if (start_blk < 16) {
            memset(data_blk, 0, BLOCK_SIZE);
            // Get the block locally
            if (my_inode.direct_ptr[start_blk] == -1) {
                my_inode.direct_ptr[start_blk] = get_avail_blkno();
				if(my_inode.direct_ptr[start_blk] == -1){
					return -ENOMEM;
				}
                writei(my_inode.ino, &my_inode);
            }
            bio_read(my_inode.direct_ptr[start_blk], data_blk);

            // Write in block
            int limit = (size - temp_size) < (BLOCK_SIZE - blk_write_loc) ? (size - temp_size) : (BLOCK_SIZE - blk_write_loc);
            memcpy(data_blk + blk_write_loc, buffer + temp_size, limit);

            // Write data block back to disk
            bio_write(my_inode.direct_ptr[start_blk], data_blk);

            temp_size += limit;
            start_blk++;
            blk_write_loc = 0;
        } else {
            // Handling indirect pointers
            // Removing direct data blocks
            start_blk -= 16;
            start_blk /= (BLOCK_SIZE / sizeof(int));
            int start_blk_db_offset = start_blk % (BLOCK_SIZE / sizeof(int));

            // Knowing the data block index and reading it
            int final_blk_num = my_inode.indirect_ptr[start_blk];
            // If the indirect pointer is not allocated, allocate it
            if (final_blk_num == -1) {
                final_blk_num = get_avail_blkno();
				if(final_blk_num == -1){
					return -ENOMEM;
				}
                my_inode.indirect_ptr[start_blk] = final_blk_num;
                writei(my_inode.ino, &my_inode);
            }

            memset(data_blk, 0, BLOCK_SIZE);
            bio_read(final_blk_num, data_blk);
            int *entries = (int *)data_blk;
            int db_to_read = entries[start_blk_db_offset];

            if (db_to_read == -1) {
                db_to_read = get_avail_blkno();
				if(db_to_read == -1){
					return -ENOMEM;
				}
                entries[start_blk_db_offset] = db_to_read;
                bio_write(final_blk_num, data_blk);
            }

            memset(data_blk, 0, BLOCK_SIZE);
            bio_read(db_to_read, data_blk);

            // Write in block
            int limit = (size - temp_size) < (BLOCK_SIZE - blk_write_loc) ? (size - temp_size) : (BLOCK_SIZE - blk_write_loc);
            memcpy(data_blk + blk_write_loc, buffer + temp_size, limit);

            // Write data block back to disk
            bio_write(db_to_read, data_blk);

            temp_size += limit;
            start_blk++;
            blk_write_loc = 0;
        }
    }

    // Step 4: Update the inode info and write it to disk
    time_t current_time = time(NULL);
    my_inode.vstat.st_atime = current_time;
    my_inode.vstat.st_mtime = current_time;
    my_inode.size += ((offset + size) - my_inode.size);

    writei(my_inode.ino, &my_inode);

    if (debugOuter)
        printf("\n---> EXITING rufs_write\n");

    // Note: this function should return the amount of bytes you write to disk
    return size;
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	if(debugOuter)
		printf("\n---> ENTERING rufs_unlink");
	memset(data_blk2, 0, BLOCK_SIZE);
	char *dir_name = (char *)malloc(strlen(path) + 1);
	memset(data_blk3, 0, BLOCK_SIZE);
	char *base_name = (char *)malloc(strlen(path) + 1);
	if(debugInner){
		printf("Original Path given: %s", path);
		printf("Original Path Len given: %d", (int)strlen(path));
	}
	dir_base_split(path, dir_name, base_name);
	if(debugInner){
		printf("\nParent Directory: %s", dir_name);
		printf("\nTarget File: %s", base_name);
	}
	
	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode final_inode;
	// Step 2: If not find, return -1
	if(get_node_by_path(path, 0, &final_inode) < 0){
		if(debugInner)
			printf("\n    -> Path not found");
		free(base_name);
		free(dir_name);
		return -1;
	}

	// Step 3: Clear data block bitmap of target file
	for(int i=0; i<16; i++){
		if(final_inode.direct_ptr[i] == -1)
			break;
		unset_bitmap(data_bitmap, final_inode.direct_ptr[i] - my_super_block->d_start_blk);
		final_inode.direct_ptr[i] = -1;
	}
	
	for(int i=0; i<8; i++){
		if(final_inode.indirect_ptr[i] == -1)
			break;
		memset(data_blk, 0, BLOCK_SIZE);
		bio_read(final_inode.indirect_ptr[i], data_blk);
		int *blk_nums = (int*)data_blk;
		int k = 0;
		while(blk_nums[k] != -1 && k < (BLOCK_SIZE/sizeof(int))){
			unset_bitmap(data_bitmap, blk_nums[k] - my_super_block->d_start_blk);
			blk_nums[k] = -1;
			k++;
		}
		unset_bitmap(data_bitmap, final_inode.indirect_ptr[i] - my_super_block->d_start_blk);
		final_inode.indirect_ptr[i] = -1;
	}

	// Step 4: Clear inode bitmap and its data block
	unset_bitmap(inode_bitmap, final_inode.ino);

	// Step 5: Call get_node_by_path() to get inode of parent directory
	struct inode dir_inode;
	if(debugInner)
		printf("     -> going to call get_node_by_path in unlink\n");
	if(get_node_by_path(dir_name, 0, &dir_inode)<0){
		if(debugOuter)
			printf("\n---> Exiting the rufs_unlink with status FAILURE\n");
		free(base_name);
		free(dir_name);
		return -ENOENT;
	}

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
	if(dir_remove(dir_inode, base_name, strlen(base_name)) < 0)
	{
		if(debugOuter)
			printf("\n---> Exiting the rufs_unlink with status FAILURE\n");
		free(base_name);
		free(dir_name);
		return -EIO;
	}

	if(debugOuter)
		printf("\n---> Exiting the rufs_unlink with status SUCCESS\n");
	free(base_name);
	free(dir_name);
	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}