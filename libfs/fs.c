#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

#define SIGNATURE_SIZE 8
#define SUPER_BLOCK_PADDING_SIZE 4079
#define DIRECTORY_PADDING_SIZE 10
#define FAT_EOC 0xFFFF
#define FILE_DIRECTORY_SIZE 32

struct file_directory {
	struct directory* fileDirectory;
	size_t offset;
	int file_open; // 0 if closed, 1 if open
};

struct super_block { // fix this, wrong variables
	char signature[SIGNATURE_SIZE];
	uint16_t total_blocks; // number of total blocks in virtual disk
	uint16_t index_root; // root directory index
	uint16_t index_data_start; // index that data blocks starts
	uint16_t num_data_blocks; // number of data blocks
	uint8_t num_fat_blocks; // number of fat blocks
	uint8_t padding[SUPER_BLOCK_PADDING_SIZE]; // ensures superblock size is 4096 bytes
} __attribute__((packed)); // prevents extra padding

struct directory {
	char name[FS_FILENAME_LEN];
	uint32_t size_of_file;
	uint16_t first_data_block; // index of first data block
	uint8_t padding[DIRECTORY_PADDING_SIZE];
} __attribute__((packed)); // prevents extra padding

struct directory root[FS_FILE_MAX_COUNT]; // root directory
uint16_t *fat_entries; // array of FAT blocks
int mounted = 0; // 1 if file system has been mounted, else 0
struct super_block superblock;
struct file_directory fdtable[FILE_DIRECTORY_SIZE]; // table to hold all file directories
int files_open = 0;

int fs_mount(const char *diskname)
{
	if(-1 == block_disk_open(diskname)){return -1;} // open disk

	// handles read error
	if(-1 == block_read(0, &superblock)){
		block_disk_close(); // closes disk if error
		return -1;
	}

	// checks if signature and block count is correct
	if(strncmp(superblock.signature, "ECS150FS", SIGNATURE_SIZE) != 0){
		block_disk_close(); // closes disk if error
		return -1;
	}
	// add check for correct block count

	// allocates memory for fat blocks
	fat_entries = malloc(BLOCK_SIZE * superblock.num_fat_blocks);
	if (NULL == fat_entries){
		block_disk_close();
 		return -1;
	}

	// read FAT blocks
	for(int i = 0; i < superblock.num_fat_blocks; i++){
		// index of block to read from
		size_t block_index = i + 1; // assumes first FAT block is at index 1

		// Reads block at block index calculated above, and reads to mem address calculated below
		if(-1 == block_read(block_index, fat_entries + (i * BLOCK_SIZE / sizeof(struct directory)))){
			free(fat_entries);
			fat_entries = NULL;
			block_disk_close();
			return -1;
		}
	}

	if(-1 == block_read(superblock.index_root, &root)){
		//free(root);
		//root = NULL;
		free(fat_entries);
		fat_entries = NULL;
		block_disk_close();
		return -1;
		}

	mounted = 1;
	return 0;
}

int fs_umount(void)
{
	// checks if the disk hasn't been mounted
	if(mounted == 0){return -1;}

	// write root directory to disk
	int directory_entries_num = (BLOCK_SIZE / sizeof(struct directory)) * superblock.index_root;
	for(int i = 0; i < directory_entries_num; i++){
		// index of block to read
		size_t block_index = i + superblock.index_root;

		// Reads block at block index calculated above, and reads to mem address calculated below
		if(-1 == block_write(block_index, (BLOCK_SIZE / sizeof(struct directory)) * i + root)){
			return -1;
		}
	}

	// write FAT blocks to disk
	for(int i = 0; i < superblock.num_fat_blocks; i++){
		// index of block to read
		size_t block_index = i + 1; // assumes first FAT block is at index 1

		// location in memory to read from
		uint16_t* mem_location = (BLOCK_SIZE / sizeof(uint16_t)) * i + fat_entries;

		if(-1 == block_write(block_index, mem_location)){
			return -1;
		}
	}

	// closes disk
	if(block_disk_close() == -1){
		return -1;
	}

	// cleans up allocated memory
	//free(root);
	//root = NULL;
	free(fat_entries);
	fat_entries = NULL;
	mounted = 0;
	return 0;

}

int fs_info(void)
{
	// checks if the disk has been mounted
	if(mounted == 0){return -1;}

	int block_count = block_disk_count();
	if(fat_entries == NULL || block_count == -1){return -1;}

	// counts number of entries that are free
	int entries_free = 0;
	int directory_entries_num = FS_FILE_MAX_COUNT;
	for(int i = 0; i < directory_entries_num; i++){
		if('\0' == root[i].name[0]){entries_free++;}
	}

	// counts free data blocks
	int data_blocks_free = 0;
	for(int i = 0; i < superblock.num_data_blocks; i++){
		if(0 == fat_entries[i]){data_blocks_free++;}
	}

	// prints all filesystem info
	printf("FS Info:\n");
	printf("total_blk_count=%d\n", block_count);
	printf("fat_blk_count=%d\n", superblock.num_fat_blocks);
	printf("rdir_blk=%d\n", superblock.index_root);
	printf("data_blk=%d\n", superblock.index_data_start);
	printf("data_blk_count=%d\n", superblock.num_data_blocks);
	printf("fat_free_ratio=%d/%d\n", data_blocks_free, superblock.num_data_blocks);
	printf("rdir_free_ratio=%d/%d\n", entries_free, FS_FILE_MAX_COUNT);

	return 0;
}

int fs_create(const char *filename)
{
    	if (block_disk_count() == -1){
        	return -1;
    	}

	int length = strlen(filename);
    	if(length > FS_FILENAME_LEN){
        	return -1;
    	}

	int index = -1;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if (root[i].name[0] == '\0' && index == -1){
			index = i;
			break;
		}
		if(strcmp(root[i].name, filename) == 0){
            		return -1;
        	}
    	}

	if(index == -1){
		return -1;
	}

	strcpy(root[index].name, filename);
	root[index].size_of_file = 0;
	root[index].first_data_block = FAT_EOC;

	return 0;
}

int fs_delete(const char *filename)
{
	int index_of_file = -1;
	uint16_t current_block; // used to loop through FAT blocks associated with file
	uint16_t next_block; // used to store next block in chain to free

    	if(block_disk_count()){return -1;}
    	if(filename == NULL){return -1;}

	// loops through all files and finds corresponding name
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(0 == strcmp(filename, root[i].name)){ // checks for corresponding file name
			index_of_file = i;
			break;
		}
	}
	if(-1 == index_of_file){return -1;} // If file does not exist in root
	// Free FAT blocks that correspond to filename
	current_block = root[index_of_file].first_data_block; // gets first block in file
	while(FAT_EOC != current_block){
		next_block = fat_entries[current_block]; // gets next block in file
		fat_entries[current_block] = 0; // clears block
		current_block = next_block; // goes to next block in file
	} 

	// clears root associated with file
	root[index_of_file].size_of_file = 0;
	root[index_of_file].first_data_block = FAT_EOC;
	memset(root[index_of_file].name, 0, FS_FILENAME_LEN);
	return 0;
}
int fs_ls(void)
{
    	if (block_disk_count()){
        	return -1;
    	}
	printf("FS Ls:\n");
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if (root[i].name[0] != '\0'){
			printf("file: %s, size: %d, data_blk: %d\n", root[i].name, root[i].size_of_file, root[i].first_data_block);
		}
	}
	return 0;
}

int fs_open(const char *filename)
{

	if (block_disk_count() == -1 || files_open >= FS_OPEN_MAX_COUNT)
	{
		return -1;
	}

	int index = -1;

	for (int i = 0; i < FS_FILE_MAX_COUNT; i++)
	{
		if(strcmp((char*)root[i].name, filename) == 0)
		{
			index = i;
			break;
		}
	}
	if(index == -1)
	{
		return -1;
	}
	for(int i = 0; i < FS_OPEN_MAX_COUNT; i++)
	{
		if(fdtable[i].file_open == 0)
		{
			fdtable[i].offset = 0;
			fdtable[i].file_open = 1;
			fdtable[i].fileDirectory = &(root[index]);
			files_open++;
			return(i);
		}
	}
	return -1;
}

int fs_close(int fd)
{
	if (block_disk_count() == -1 || fd >= FS_OPEN_MAX_COUNT || fdtable[fd].file_open == 0)
	{
		return -1;
	}
	fdtable[fd].file_open = 0;
	files_open--;
	return 0;
}

int fs_stat(int fd)
{
	if (block_disk_count() == -1 || fd >= FS_OPEN_MAX_COUNT || fdtable[fd].file_open == 0)
	{
		return -1;
	}
	return fdtable[fd].fileDirectory->size_of_file;
}

int fs_lseek(int fd, size_t offset)
{
	if(fs_stat(fd) == -1 || offset > (size_t)fs_stat(fd))
	{
		return -1;
	}
	fdtable[fd].offset = offset;
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	// error handling for fd
	if(fd >= FS_OPEN_MAX_COUNT || fd < 0){return -1;}
	
	// error handling for buf
	if(buf == NULL){return -1;}

	// error handling for count
	if(count > fdtable[fd].fileDirectory->size_of_file){return -1;}

	return 0;
}

/* Helper function to find the index of a block in a file */
int find_block_index(size_t offset, int fd){
	int index_of_block = fdtable[fd].fileDirectory->first_data_block; // finds first block in file
	// iterate through FAT chain to find block
	for(size_t i = 0; i < offset / BLOCK_SIZE; i++){
		index_of_block = fat_entries[index_of_block];
	}
	return index_of_block;
}

/* Helper function to find the minimum number */
size_t min(size_t num1, size_t num2){
	if(num1 < num2){return num1;}else{return num2;}
}

int fs_read(int fd, void *buf, size_t count)
{
	uint8_t* buffer = (uint8_t*) buf; // buffer to store read into
	size_t read_counter = 0; // counts number of reads done

	// Error handling
	if(fdtable[fd].file_open == 0 || fd < 0 || fd >= FILE_DIRECTORY_SIZE){return -1;}
	if(buf == NULL || count == 0){return -1;}

	while(fdtable[fd].fileDirectory->size_of_file >= fdtable[fd].offset && count > read_counter){
		uint8_t block_buffer[BLOCK_SIZE]; // temporary buffer for each block

		// reads block using given file descriptor
		if(-1 == block_read(find_block_index(fdtable[fd].offset, fd), block_buffer)){
			break;
		}

		int position_in_block = fdtable[fd].offset % BLOCK_SIZE; // calculates location in block based on offset
		size_t size_of_block = min(BLOCK_SIZE - position_in_block, count - read_counter); // calculates size
		// moves data from temporary block buffer to buffer passed in to function
		memcpy(buffer + read_counter, block_buffer + position_in_block, size_of_block);

		// changes each variable based on how many bytes were in the block that was just read from
		fdtable[fd].offset += size_of_block;
		//buffer = size_of_block + buffer;
		read_counter += size_of_block;
	}
	return read_counter;
}
