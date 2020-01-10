#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <bsd/string.h>
#include "disk_emu.h"
#include "sfs_api.h"


/*
* Alexa Normandin
*
*
* ############## LIBBDS Must be installed for using strlcpy ##################
* run sudo apt-get install libbsd-dev
*/



#define NUM_BLOCKS 102399				
#define NUM_INODES 256					
#define MAX_FILES_OPEN 100				
#define BLOCK_SIZE 1024
#define START_ADDRESS_SB 0	
#define FREE_MAP_BLOCKS 101					
#define START_ADDRESS_INODE 1				
#define INODE_SIZE 72						
#define INODE_BLOCKS 18				
#define ROOT_DIR_INODE_NBR 0				
#define START_ADDRESS_DIR 19				
#define NBR_OF_DIR_BLOCKS 7					
#define START_ADDRESS_FREE 26				
#define NUM_PTRS 12		

#define INDR_PTRS 256		

#define MODE 0
#define LINK_CNT 1
#define UID 0
#define GID 0


int DIRECTORY_WALKER = 1;					


//Super block struct
typedef struct {
	int magic_number;
	int block_size;
	int fs_size;
	int inode_table_length;
	int root_dir;
} SuperBlock_Struct;


//iNode struct  (6*4 + 4*12bytes = 72 bytes/inode)
typedef struct {
	int mode;
	int link_cnt;
	int uid;
	int gid;
	int size;
	int pointers[12];
	int indir_pointer;
} iNode_Struct;


//Directory entry struct  (4 + 20*1 + 4 = 28 bytes/entry)
typedef struct {
	int inode_num;
	char filename[21];
	char used;
	//short buffer;
} Directory_Entry_Struct;


//Open file entry structure
typedef struct {
	int inode_num;
	char filename[21];
	int read_pointer;
	int write_pointer;
	int used;
} Open_File_Struct;



//Data structures in memory

char Mem_FreeBitmap[NUM_BLOCKS + 1];
Directory_Entry_Struct Mem_DirectoryTable[NUM_INODES];
iNode_Struct Mem_inodeTable[NUM_INODES];
Open_File_Struct openFileDescriptorTable[MAX_FILES_OPEN];

void mksfs(int fresh){

	if (fresh == 0) 	
		init_disk("./sfs", BLOCK_SIZE, NUM_BLOCKS);			//get old saved disk

	else {
		init_fresh_disk("./sfs", BLOCK_SIZE, NUM_BLOCKS);		//create new blank disk
		
		
		//SUPER BLOCK
		SuperBlock_Struct Super_Block = {0xACBD0005, BLOCK_SIZE, NUM_BLOCKS, INODE_BLOCKS, ROOT_DIR_INODE_NBR};
		write_blocks(START_ADDRESS_SB, 1, &Super_Block);		
	

		//INODE TABLE
		iNode_Struct blank_inode;
		blank_inode.mode = MODE;
		blank_inode.link_cnt = LINK_CNT;
		blank_inode.uid = UID;
		blank_inode.gid = GID;
		blank_inode.size = 0;
		blank_inode.pointers[1] = 0;
		blank_inode.pointers[2] = 0;
		blank_inode.pointers[3] = 0;
		blank_inode.pointers[4] = 0;
		blank_inode.pointers[5] = 0;
		blank_inode.pointers[6] = 0;
		blank_inode.pointers[7] = 0;
		blank_inode.pointers[8] = 0;
		blank_inode.pointers[9] = 0;
		blank_inode.pointers[10] = 0;
		blank_inode.pointers[11] = 0;
		blank_inode.indir_pointer = 0;
			
		iNode_Struct inode_table[NUM_INODES];
		for (int i = 0; i < NUM_INODES; i++){
			inode_table[i] = blank_inode;
		}
		

		//DIRECTORY TABLE
		Directory_Entry_Struct directory_table[NUM_INODES];		
		for (int i = 0; i < NUM_INODES; i++){							
			directory_table[i].inode_num = i;
			strlcpy(directory_table[i].filename, "", 1);		
			directory_table[i].used = '0';
		}
		strlcpy(directory_table[0].filename, "./sfs\0", 21);				
		directory_table[0].used = '1';
		

		//FREE BIT MAP
		char FreeBitMap[NUM_BLOCKS + 1];				
		for (int i = 0; i < NUM_BLOCKS; i++){	
			FreeBitMap[i] = '0';
		}

		for (int i = 0; i < (1 + INODE_BLOCKS + NBR_OF_DIR_BLOCKS + FREE_MAP_BLOCKS); i++){	
			FreeBitMap[i] = '1';
		}
		FreeBitMap[NUM_BLOCKS] = '\0';


		write_blocks(START_ADDRESS_DIR, NBR_OF_DIR_BLOCKS, directory_table);
		write_blocks(START_ADDRESS_INODE, INODE_BLOCKS, inode_table);
		write_blocks(START_ADDRESS_FREE, FREE_MAP_BLOCKS, FreeBitMap);
	}	
		


	//LOAD INTO MEM
	read_blocks(START_ADDRESS_INODE, INODE_BLOCKS, Mem_inodeTable);
	read_blocks(START_ADDRESS_DIR, NBR_OF_DIR_BLOCKS, Mem_DirectoryTable);
	read_blocks(START_ADDRESS_FREE, FREE_MAP_BLOCKS, Mem_FreeBitmap);
	

	//OPEN FILE DESCRIPTOR TABLE
	for (int i = 0; i < MAX_FILES_OPEN; i++){
		openFileDescriptorTable[i].used = 0;
	}

}

int sfs_fread(int fileId, char *buf, int length){


	if (openFileDescriptorTable[fileId].used == 0){
		return -1;
	}

	int inode_num = openFileDescriptorTable[fileId].inode_num;
	iNode_Struct file_inode = Mem_inodeTable[openFileDescriptorTable[fileId].inode_num];
	int read_pointer = openFileDescriptorTable[fileId].read_pointer;

	int file_size = file_inode.size;

	if (inode_num == (-1)){
		//no inode
		return -1;
	}

	//check if file has data
	if (file_size == 0){
		return 0;
	}

	
	if (file_size == read_pointer){
		return 0;
	}

	int nbr_blocks_to_read = file_size / BLOCK_SIZE;		
    if ((file_size % BLOCK_SIZE) != 0){
        nbr_blocks_to_read ++;
    }

	void *old_data = malloc (nbr_blocks_to_read * BLOCK_SIZE);			

	for(int i = 0; ((i < 12) && (i < nbr_blocks_to_read)); i++) {	
		read_blocks(file_inode.pointers[i], 1, old_data + (i*BLOCK_SIZE));
	}

	//If file needed more than 12 blocks
	if (nbr_blocks_to_read > 12){

		int indir_pointer_block[INDR_PTRS];

		read_blocks(file_inode.indir_pointer, 1, indir_pointer_block);

		int read_blocks_rem = nbr_blocks_to_read - 12;
		for (int i = 0; read_blocks_rem > 0; i++){
			read_blocks(indir_pointer_block[i], 1, old_data + ((i+12)*BLOCK_SIZE));
            read_blocks_rem --;
		}
	}

	//number of bytes to be read, check if enough bytes available to read
	int length_read = length;
	if ((length + read_pointer) > file_size){
		length_read = file_size - read_pointer - 1;
	}

	void *new_data = malloc(length_read); 

	//read bytes into other array
    int next_read_pointer = read_pointer + length_read;
    memcpy(new_data, old_data + read_pointer, length_read);

	openFileDescriptorTable[fileId].read_pointer = next_read_pointer;	
	memcpy(buf, new_data, length_read);

    free(old_data);
    free(new_data);
	return length_read;			
}


int sfs_fwrite(int fileID, char *buf, int length){
	
	if (openFileDescriptorTable[fileID].used == 0){
		return -1;
	}

	if (length == 0){
		return 0;
	}

	int file_inode_nbr = openFileDescriptorTable[fileID].inode_num;
	int old_file_size = Mem_inodeTable[file_inode_nbr].size;
	int start_write_pointer = openFileDescriptorTable[fileID].write_pointer;

	//check how many blocks we will need
	int new_file_size = 0;
	if ((start_write_pointer + length) <= old_file_size){
		new_file_size = old_file_size;
	}
	else {
		new_file_size = start_write_pointer + length;
	}
	if (old_file_size == 0){
		new_file_size++;
	}

	int old_blocks = old_file_size / BLOCK_SIZE;		
    if ((old_file_size % BLOCK_SIZE) != 0){
        old_blocks ++;
    }
	int blocks_needed = new_file_size / BLOCK_SIZE;		
    if ((new_file_size % BLOCK_SIZE) != 0){
        blocks_needed++;
    }

    if (blocks_needed > NUM_PTRS + (BLOCK_SIZE / 4)){
        return -1;
    }

	void *file_data = malloc (blocks_needed * BLOCK_SIZE);				//create array for file data

	for(int i = 0; ((i < 12) && (i < old_blocks)); i++) {	

		read_blocks(Mem_inodeTable[file_inode_nbr].pointers[i], 1, file_data + (i*BLOCK_SIZE));
	}

	int indir_pointer_block[INDR_PTRS];		
	if (old_blocks > 12){
		
		read_blocks(Mem_inodeTable[file_inode_nbr].indir_pointer, 1, indir_pointer_block);

		int nbr_blocks_to_read_remaining = old_blocks - 12;
		for (int i = 0; nbr_blocks_to_read_remaining > 0; i++){
			
			read_blocks(indir_pointer_block[i], 1, file_data + ((i+12)*BLOCK_SIZE));
			nbr_blocks_to_read_remaining--;
		}
	}


	//write in new bytes into file
    int next_write_pointer = start_write_pointer + length;
    memcpy(file_data + start_write_pointer, buf, length);


	//if indir pointer is being used for first time
	if ((old_blocks < 13) && (blocks_needed >=13)){
		//find block for indir pointer
		for (int i = 0; i < NUM_BLOCKS; i++){
			if (Mem_FreeBitmap[i] == '0'){
				Mem_FreeBitmap[i] = '1';
				Mem_inodeTable[file_inode_nbr].indir_pointer = i;
				break;
			}
		}
	}

	//find new blocks (including 13th block pointing to newer if necessary)
	for (int j = old_blocks; j < blocks_needed; j++){
		for (int i = 0; i < NUM_BLOCKS; i++){
			if (Mem_FreeBitmap[i] == '0'){
				Mem_FreeBitmap[i] = '1';
				if (j < 12){
					Mem_inodeTable[file_inode_nbr].pointers[j] = i;		//set pointer to new block
				}
				else {
					indir_pointer_block[j-12] = i;	//add pointer to block in indir pointer block
				}
				break;
			}
		}
	}

	for (int i = 0; i < blocks_needed; i++){
		//if block pointed by dir pointer
		if (i < 12){
			write_blocks(Mem_inodeTable[file_inode_nbr].pointers[i], 1, file_data + (i*BLOCK_SIZE));
		}
		//if first block pointed by indir pointer
		else if (i == 12){
			write_blocks(indir_pointer_block[i-12], 1, file_data + (i*BLOCK_SIZE));

			write_blocks(Mem_inodeTable[file_inode_nbr].indir_pointer, 1, indir_pointer_block);
		}
		else {
			write_blocks(indir_pointer_block[i-12], 1, file_data + (i*BLOCK_SIZE));
			
		}
	}

	//update in mem inode table
	Mem_inodeTable[file_inode_nbr].size = new_file_size;
	
	write_blocks(START_ADDRESS_INODE, INODE_BLOCKS, Mem_inodeTable);
    write_blocks(START_ADDRESS_FREE, FREE_MAP_BLOCKS, Mem_FreeBitmap);
    
	
	//update write pointer location
	openFileDescriptorTable[fileID].write_pointer = next_write_pointer;

	//return length
    free(file_data);
	return length;
}



int sfs_fopen(char *name){

	int in = strcmp(name, "");
	if (in == 0){
		return -1;
	}

	if (strlen(name) > 21){
		return -1;
	}


	//Check if file is already open
	for (int i = 0; i < MAX_FILES_OPEN; i++){
		if (openFileDescriptorTable[i].used == 1){		//check to not compare a null string
			if (strcmp(name, openFileDescriptorTable[i].filename) == 0){
				return i;
			}
		}
	}


	for (int i = 0; i < NUM_INODES; i++){

		if (strcmp(name, Mem_DirectoryTable[i].filename) == 0){
			for (int j = 0; j < MAX_FILES_OPEN; j++){
				if (openFileDescriptorTable[j].used == 0){					

					openFileDescriptorTable[j].used = 1;			
					strlcpy(openFileDescriptorTable[j].filename, name, 21);	//set file name
					openFileDescriptorTable[j].inode_num = Mem_DirectoryTable[i].inode_num;		
					openFileDescriptorTable[j].read_pointer = 0;	
					openFileDescriptorTable[j].write_pointer = Mem_inodeTable[openFileDescriptorTable[j].inode_num].size;
					return j;
				}
			}
			return -1;
		}
	}

	
	//Find an unused i-node in the file directory table
	for (int i = 0; i < NUM_INODES; i++){
		if (Mem_DirectoryTable[i].used == '0') {

		
			for (int j = 0; j < MAX_FILES_OPEN; j++){
				if (openFileDescriptorTable[j].used == 0){
					openFileDescriptorTable[j].used = 1;			
					memcpy(openFileDescriptorTable[j].filename, name, 21);	
					openFileDescriptorTable[j].inode_num = Mem_DirectoryTable[i].inode_num;		//same inode number
					openFileDescriptorTable[j].read_pointer = 0;	
					openFileDescriptorTable[j].write_pointer = 0;	


					Mem_DirectoryTable[i].used = '1';
					strlcpy(Mem_DirectoryTable[i].filename, name, 21);
					write_blocks(START_ADDRESS_DIR, NBR_OF_DIR_BLOCKS, Mem_DirectoryTable);

					return j;
				}
			}

		
		}
	}
	
	//if here: no more place for open files:
	return -1;

}


int sfs_fclose(int fileID){

	if (openFileDescriptorTable[fileID].inode_num == -1){
		return -1;
	}

	if ((fileID < 0) || (fileID >= MAX_FILES_OPEN)){	//invalid file id
		return -1;
	}
	else if (openFileDescriptorTable[fileID].used == 0){	//file not open
		return -1;
	}

	openFileDescriptorTable[fileID].used = 0;
	strlcpy(openFileDescriptorTable[fileID].filename, "", 1);
	openFileDescriptorTable[fileID].read_pointer = 0;
	openFileDescriptorTable[fileID].write_pointer = 0;
	return 0;
}


int sfs_frseek(int fileID, int loc){

	if (loc < 0) {
		return -1;
	}
	int size = Mem_inodeTable[openFileDescriptorTable[fileID].inode_num].size;	
	if (loc > size) {
		return -1;
	}
	openFileDescriptorTable[fileID].read_pointer = loc;
	return 0;
}


int sfs_fwseek(int fileID, int loc){

	if (loc < 0) {
		return -1;
	}
	int size = Mem_inodeTable[openFileDescriptorTable[fileID].inode_num].size;	
	if (loc > size) {
		return -1;
	}
	openFileDescriptorTable[fileID].write_pointer = loc;
	return 0;
}



int sfs_getnextfilename(char *fname){
	
	//starting from directory walker index to end of table
	for (int i = DIRECTORY_WALKER; i < NUM_INODES; i++){
		if (Mem_DirectoryTable[i].used == '1'){			//if the entry is being used
			memcpy(fname, Mem_DirectoryTable[i].filename, 21);		//store file name in parameter
			DIRECTORY_WALKER = i + 1;					//update directory walker for next call
			return 1;
		}
	}
	//if no new file, update directory walker for next time
	DIRECTORY_WALKER = 1;		//starts at 1 since we will not return the first file (ie root dir)
	return 0;

}

int sfs_getfilesize(const char* path){

	for (int i = 0; i < NUM_INODES; i++){
		if (strcmp(path, Mem_DirectoryTable[i].filename) == 0){
			int size = Mem_inodeTable[Mem_DirectoryTable[i].inode_num].size;
			return size;
		}
	}

	//file doesn't exist
	return -1;
}


int sfs_remove(char *file){

	//check if file is open
	for (int i = 0; i < MAX_FILES_OPEN; i++){
		if (strcmp(openFileDescriptorTable[i].filename, file) == 0){
			return -1;		
		}
	}

	
	int file_inode_nbr = -1;
	for (int i = 0; i < MAX_FILES_OPEN; i++){
		if (strcmp(Mem_DirectoryTable[i].filename, file) == 0){
			file_inode_nbr = Mem_DirectoryTable[i].inode_num;		

			Mem_DirectoryTable[i].used = '0';
			strlcpy(Mem_DirectoryTable[i].filename, "", 1);
			break;
		}
	}

	//check if file exists
	if (file_inode_nbr == -1){
		return -1;
	}

	int file_size = Mem_inodeTable[file_inode_nbr].size;
	int blocks_used_num = ceil (file_size / BLOCK_SIZE);

	int indir_pointer_block[INDR_PTRS];		//global" to make it easier to reference

	//if indir pointer used, get pointer in block
	if (blocks_used_num > 12){
		read_blocks(Mem_inodeTable[file_inode_nbr].indir_pointer, 1, &indir_pointer_block);
		Mem_inodeTable[file_inode_nbr].indir_pointer = 0;		//ckear indir pointer in inode
	}

	//clear in mem inode table and in mem free bit map
	for (int i = 0; i < blocks_used_num; i++){
		if (i < 12){
			Mem_FreeBitmap[Mem_inodeTable[file_inode_nbr].pointers[i]] = '0';
			Mem_inodeTable[file_inode_nbr].pointers[i] = 0;
		}
		else{
			Mem_FreeBitmap[indir_pointer_block[i-12]] = '0';
		}
	}

	Mem_inodeTable[file_inode_nbr].size = 0;


	write_blocks(START_ADDRESS_INODE, INODE_BLOCKS, &Mem_inodeTable);
	write_blocks(START_ADDRESS_FREE, FREE_MAP_BLOCKS, &Mem_FreeBitmap);
	write_blocks(START_ADDRESS_DIR, NBR_OF_DIR_BLOCKS, &Mem_DirectoryTable);

	return 0;
}

