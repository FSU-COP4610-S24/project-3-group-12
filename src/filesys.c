#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>

#define ATTR_READ_ONLY   0x01
#define ATTR_HIDDEN      0x02
#define ATTR_SYSTEM      0x04
#define ATTR_VOLUME_ID   0x08
#define ATTR_DIRECTORY   0x10
#define ATTR_ARCHIVE     0x20

void displayPrompt(char * imageFileName, char *currentDirectory);
char *get_input(void);
tokenlist *new_tokenlist(void);
void add_token(tokenlist *tokens, char *item);
tokenlist *get_tokens(char *input);
void free_tokens(tokenlist *tokens);
void setFATEntry(uint32_t clusterNumber, uint32_t value);
void open_file_for_read(const char* filename);
void read_data_from_file(const char* filename, int size);
void write_data_to_file(const char* filename, const char* data);
char* getDirectoryNameByCluster(uint32_t parentCluster, uint32_t targetCluster);
bool changeDirectory(const char *dirname);
bool makeDirectory(const char *dirname);
bool createFile(const char *filename);
bool removeFile(const char *filename);
uint32_t getClusterNumber(char *path);
uint32_t getFATEntry(uint32_t clusterNumber);
uint32_t findClusterInDirectory(uint32_t directoryCluster, char *name);
uint32_t allocateNewCluster();
uint32_t convert_cluster_to_offset(uint32_t cluster);
uint32_t compute_dentry_offset(uint32_t clusterNumber, const char* filename);
bool is_directory_empty(uint32_t directoryCluster);
void remove_directory_entry(uint32_t directoryCluster, char *path);
bool remove_empty_directory(uint32_t directoryCluster, char *path);

struct imageStruct {
    int fd;
    uint16_t BpSect;
    uint8_t sectpClus;
    uint8_t numFATs;
    int16_t rsvSecCnt;
    uint32_t secpFAT;
    uint32_t rootClus;
    uint32_t totalSec;
    uint32_t totalDataClus;
    uint32_t entpFAT;
    uint32_t dataStartOffset;
    int64_t size;
};

typedef struct __attribute__((packed)) directoryEntry {
    char DIR_Name[11];
    uint8_t DIR_Attr;
    char padding_1[8]; //unused fields
    uint16_t DIR_FstClusHI;
    char padding_2[4]; //unused fields
    uint16_t DIR_FstClusLO;
    uint32_t DIR_FileSize;
} directoryEntry;

typedef struct {
    char name[11];
    int is_open;
    uint32_t start_cluster;
    uint32_t size;
    uint32_t offset;
    uint8_t access_mode;
} open_file;

struct imageStruct *image;
struct directoryEntry *directoryEntries = NULL;
open_file *opened_files = NULL;
int numOpenedFiles = 0;
int numDirectoryEntries = 0;

directoryEntry* find_file_in_directory(const char* filename);

//used to manage what directory we are in and path information
char* currentDirectory;
int currentClusterNumber = 2;
uint32_t *clusterPath = NULL;
int pathIndex = -1;
int pathSize = 0;

int main(int argc, char *argv[]) {
    char command[100];
    int status;

    if (argc != 2) {
        printf("Argument error: ./filesys <FAT32 image file>\n");
        return 1;
    }

    sprintf(command, "sudo mount -o loop %s ./mnt ", argv[1]);
    status = system(command);
    if (status == -1) {
        perror("mount failed");
        return 1;
    }

    image = malloc(sizeof(struct imageStruct));
    image->fd = open(argv[1], O_RDWR);
    struct stat fileInfo;
    stat(argv[1], &fileInfo);
    image->size = (int64_t)fileInfo.st_size;
    initImage(image);

    while (1) {
        displayPrompt(argv[1], currentDirectory);
        char *input = get_input();
        tokenlist *tokens = get_tokens(input);

        if (strcmp(tokens->items[0], "exit") == 0) {
            free(input);
	    free(tokens);
	    sprintf(command, "sudo umount ./mnt");
	    system(command);
            break;
        }
        if (strcmp(tokens->items[0], "info") == 0) {
            printImageStruct(image);
        }
        if (strcmp(tokens->items[0], "ls") == 0) {
            if (tokens->size >= 2) {
                listDirectoryEntries(tokens->items[1]);
            } else {
                listDirectoryEntries("");
            }
        }
        if (strcmp(tokens->items[0], "cd") == 0) {
            if (tokens->size >= 2) {
		changeDirectory(tokens->items[1]);
           }
        }
	if (strcmp(tokens->items[0], "mkdir") == 0) {
	    if (tokens->size >= 2) {
		makeDirectory(tokens->items[1]);
	    }
	}
	if (strcmp(tokens->items[0], "creat") == 0) {
	    if (tokens->size >= 2) {
		createFile(tokens->items[1]);
	    }
	}
	if (strcmp(tokens->items[0], "open") == 0) {
	    if (tokens->size >= 2) {
		char *filename = tokens->items[1];
		open_file_for_read(filename);
	    }
	}
	if (strcmp(tokens->items[0], "close") == 0) {
	    if (tokens->size >= 2) {
		char *filename = tokens->items[1];
		closeFile(filename);
	    }
	}
	if (strcmp(tokens->items[0], "read") == 0) {
	    if (tokens->size >= 3) {
		char *filename = tokens->items[1];
		int size = atoi(tokens->items[2]);
		read_data_from_file(filename, size);
	    }
	}
	if (strcmp(tokens->items[0], "write") == 0) {
            if (tokens->size >= 3) {
                char *filename = tokens->items[1];
                char *data = tokens->items[2];
                write_data_to_file(filename, data);
            }
        }
	if (strcmp(tokens->items[0], "rm") == 0) {
	    if (tokens->size >= 2) {
		char *filename = tokens->items[1];
		removeFile(filename);
	    }
	}
	if (strcmp(tokens->items[0], "rmdir") == 0) {
            if (tokens->size >= 2) {
                char *target = tokens->items[1];
		remove_empty_directory(currentClusterNumber, target);
	    }
	}
        free(input);
        free_tokens(tokens);
    }

    return 0;
}

bool compareDirectoryEntryName(const char *dirName, const char *inputName) {
    //iterate through each character in the directory entry name
    for (int j = 0; j < 11; ++j) {
        if (dirName[j] == ' ' && inputName[j] == '\0') {
            return true;
        }
        if (dirName[j] != inputName[j]) {
            return false;
        }
    }
    return true;
}

bool changeDirectory(const char *dirname) {
    // handle case where we change to parent directory
    if (strcmp(dirname, "..") == 0) {
        if (pathIndex >= 0) {
            currentClusterNumber = clusterPath[pathIndex];
            pathIndex--;
            // update currentDirectory to remove the last directory name
            char *lastSlash = strrchr(currentDirectory, '/');
            if (lastSlash != NULL) {
                *lastSlash = '\0';
            } else {
                // if no slash is found, reset currentDirectory
                free(currentDirectory);
                currentDirectory = NULL;
            }
            return true;
        } else {
            return false;
        }
    }
    
    uint32_t newCluster = findClusterInDirectory(currentClusterNumber, dirname);
    if (newCluster == 0) {
	printf("Error: Directory not found.\n");
	return false;
    }

    currentClusterNumber = newCluster;
    uint32_t parentCluster = currentClusterNumber;

    char *newPath = NULL;
    if (currentDirectory != NULL) {
	newPath = (char *)malloc(strlen(currentDirectory) + strlen(dirname) + 2);
	strcpy(newPath, currentDirectory);
	strcat(newPath, "/");
	strcat(newPath, dirname);
    }
    else {
	newPath = (char *)malloc(strlen(dirname) + 1);
	strcpy(newPath, dirname);
    }

    free(currentDirectory);
    currentDirectory = newPath;

    // store current cluster number in array and reallocate memory if necessary
    if (pathIndex >= pathSize - 1) {
        uint32_t *temp = (uint32_t)realloc(clusterPath, (pathSize + 1) * sizeof(uint32_t));
        if (temp == NULL) {
            printf("Memory allocation error.\n");
            return false;
        }
        clusterPath = temp;
        pathSize++;
    }
    // store current cluster number
    pathIndex++;
    clusterPath[pathIndex] = currentClusterNumber;

    return true;
}

    // store current cluster number in array and reallocate memory if necessary
    if (pathIndex >= pathSize - 1) {
        // reallocate memory for the path size
        int newSize = pathSize == 0 ? 1 : 2 * pathSize; // Double the size
        uint32_t *temp = realloc(clusterPath, newSize * sizeof(uint32_t));
        if (temp == NULL) {
            printf("Memory allocation error.\n");
            return false;
        }
        clusterPath = temp;
        pathSize = newSize;
    }
    // store current cluster number
    pathIndex++;
    clusterPath[pathIndex] = currentClusterNumber;

    uint32_t newCluster = getClusterNumber(dirname);
    if (newCluster == 0) {
        return false;
    }

    // update global cluster var
    currentClusterNumber = newCluster;
    uint32_t parentCluster = currentClusterNumber;

    char *newPath = NULL;
    if (currentDirectory != NULL) {
        // allocate memory for the new path with '/' appended to currentDirectory
        newPath = malloc(strlen(currentDirectory) + strlen(dirname) + 2);
        if (newPath == NULL) {
            printf("Memory allocation error\n");
            return false;
        }
        // construct the new path with '/' appended to currentDirectory
        strcpy(newPath, currentDirectory);
        strcat(newPath, "/");
        strcat(newPath, dirname);
    } else {
        // allocate memory for just the dirname
        newPath = malloc(strlen(dirname) + 1);
        if (newPath == NULL) {
            printf("Memory allocation error\n");
            return false;
        }
        strcpy(newPath, dirname);
    }

    // update global currentDirectory var
    free(currentDirectory);
    currentDirectory = newPath;

    return true;
}


uint32_t getClusterNumber(char *path){
	char *path_copy = strdup(path);
	uint32_t currentCluster = currentClusterNumber;
	char *token = strtok(path, "/");

	while (token != NULL){
		uint32_t nextCluster = findClusterInDirectory(currentCluster,token);
		
		if (nextCluster == 0){
			printf("Directory '%s' not found.\n", token);
            		return 0;
		}
		currentCluster = nextCluster;
        	token = strtok(NULL, "/");
	}
	return currentCluster;
}

uint32_t findClusterInDirectory(uint32_t directoryCluster, char *name) {
    //load directory entries for the specified cluster
    loadDirectoryEntries(directoryCluster);

    if (directoryEntries == NULL) {
        printf("Error: directoryEntries is NULL.\n");
        return 0; // Return an appropriate value indicating error
    }

    //iterate through directory entries to find the matching one
    for (int i = 0; i < numDirectoryEntries; ++i) {
        if (compareDirectoryEntryName(directoryEntries[i].DIR_Name, name)) {
            if (directoryEntries[i].DIR_Attr == ATTR_DIRECTORY) {
                uint32_t cluster_num = (uint32_t)(((uint32_t)directoryEntries[i].DIR_FstClusHI << 16) | directoryEntries[i].DIR_FstClusLO);
                return cluster_num;
            } else {
                printf("'%s' is not a directory.\n", name);
                return 0;
            }
        }
    }

    return 0;
}

uint32_t getFATEntry(uint32_t clusterNumber) {
    uint32_t offset = image->rsvSecCnt * image->BpSect + clusterNumber * 4;
    uint32_t fatEntry;

    lseek(image->fd, offset, SEEK_SET);
    read(image->fd, &fatEntry, 4);

    return fatEntry & 0x0FFFFFFF;
}

uint32_t allocateNewCluster() {
    for (uint32_t i = 2; i < image->totalDataClus; i++) {
	uint32_t fatEntry = getFATEntry(i);
	if (fatEntry == 0) {
	    setFATEntry(i, 0x0FFFFFFF);
	    return i;
	}
    }
    printf("Error: No free clusters.\n");
    return 0;
}

uint32_t convert_cluster_to_offset(uint32_t cluster) {
    uint32_t cluster_size = image->sectpClus * image->BpSect;
    uint32_t offset = image->dataStartOffset + (cluster - 2) * cluster_size;
    return offset;
}

void setFATEntry(uint32_t clusterNumber, uint32_t value) {
    uint32_t offset = image->rsvSecCnt * image->BpSect + clusterNumber * 4;

    lseek(image->fd, offset, SEEK_SET);
    write(image->fd, &value, 4);
}


directoryEntry* encode_dir_entry(int fat32_fd, uint32_t offset) {
    directoryEntry *dentry = (directoryEntry*)malloc(sizeof(directoryEntry));
    ssize_t rd_bytes = pread(fat32_fd, (void*)dentry, sizeof(directoryEntry), offset);

    if (rd_bytes != sizeof(directoryEntry)) {
        fprintf(stderr, "Error: Failed to read directory entry at offset 0x%x\n", offset);
        free(dentry);
        return NULL;
    }

    return dentry;
}

int is_valid_name(uint8_t *name) {
    return 1;
}

uint32_t convert_clus_num_to_offset_in_fat_region(uint32_t clus_num) {
    uint32_t fat_region_offset = 0x4000;
    return fat_region_offset + clus_num * 4;
}

uint32_t getNextCluster(uint32_t currentCluster) {
    uint32_t fat32_fd = image->fd;
    uint32_t max_clus_num = (image->secpFAT * image->BpSect) / image->sectpClus;
    uint32_t offset = convert_clus_num_to_offset_in_fat_region(currentCluster);
    uint32_t next_clus_num = 0;

    if (currentCluster >= 2 && currentCluster <= max_clus_num) {
        offset = convert_clus_num_to_offset_in_fat_region(currentCluster);
        ssize_t bytesRead = pread(fat32_fd, &next_clus_num, sizeof(uint32_t), offset);
        if (bytesRead == -1) {
            perror("Error reading FAT entry");
            return 0;
        }
        next_clus_num &= 0x0FFFFFFF;
        //check for the end of the chain
	if (next_clus_num >= 0x0FFFFFF8 && next_clus_num <= 0x0FFFFFFF) {
	    return 0; 
	}
        currentCluster = next_clus_num;
    }
    return next_clus_num;
}

void loadDirectoryEntries(uint32_t clusterNumber) {
    int fat32_fd = image->fd;
    uint32_t clusterSize = image->sectpClus * image->BpSect;

    //reset global list of entries
    if (directoryEntries != NULL) {
        free(directoryEntries);
        directoryEntries = NULL;
    }
    numDirectoryEntries = 0;

    //read directory entries from the current cluster and its subsequent clusters
    while (clusterNumber >= 2) {
        uint32_t offset = image->dataStartOffset + (clusterNumber - 2) * clusterSize;
	uint32_t end_offset = image->dataStartOffset + (clusterNumber - 2) * clusterSize + clusterSize;
        //read directory entries from the current cluster
        while (1) {
            //reached the end of a cluster
            if (offset + sizeof(directoryEntry) > end_offset){
            	break;
            }
            directoryEntry *entry = encode_dir_entry(fat32_fd, offset);
            if (entry->DIR_Attr == 0x0F) { //skip long file entries
                offset += sizeof(directoryEntry);
                continue;
            }
            //check if the entry is a valid file name
            if (is_valid_name(entry->DIR_Name)) {
                if (directoryEntries == NULL) {
                    directoryEntries = malloc(sizeof(directoryEntry));
                    if (directoryEntries == NULL) {
                        perror("Error allocating memory for directory entries");
                        return;
                    }
                } else {
                    //reallocate memory for directoryEntries
                    directoryEntry *temp = realloc(directoryEntries, (numDirectoryEntries + 1) * sizeof(directoryEntry));
                    if (temp == NULL) {
                        perror("Error reallocating memory for directory entries");
                        return;
                    }
                    directoryEntries = temp;
                }

                //copy the entry to directoryEntries
                directoryEntries[numDirectoryEntries++] = *entry;
            }
            offset += sizeof(directoryEntry);
        }

        //get next cluster in the chain
        clusterNumber = getNextCluster(clusterNumber);
    }
}

void listDirectoryEntries(const char *path) {
    
    //compute the cluster number for the specified directory path
    uint32_t currentCluster = getClusterNumber(path);
    if (currentCluster == 0) {
        return;
    }
    //store the directory entries in global list
    loadDirectoryEntries(currentCluster);

    //print directory entries from the global variable
    for (int i = 0; i < numDirectoryEntries; i++) {
	char name[11];
	memcpy(name, directoryEntries[i].DIR_Name, 11);
	name[11] = '\0'; 
	if ((name[0] == 0x00 || name[0] == 0x20) || name[0] == 0xE5){
		continue;
	}
	printf("%s\n", name);
    }
    
}

void open_file_for_read(const char* filename) {
    loadDirectoryEntries(currentClusterNumber);
    directoryEntry* file_entry = find_file_in_directory(filename);

    if (file_entry == NULL) {
	printf("Error: File '%s' not found.\n", filename);
	return;
    }

    for (int i = 0; i < numOpenedFiles; ++i) {
	if (strcmp(opened_files[i].name, filename) == 0 && opened_files[i].is_open) {
	    printf("Error: File '%s' is already open.\n", filename);
	    return;
	}
    }

    open_file new_file;
    strncpy(new_file.name, filename, 11);
    new_file.is_open = 1;
    new_file.start_cluster = (file_entry->DIR_FstClusHI << 16) | file_entry->DIR_FstClusLO;
    new_file.size = file_entry->DIR_FileSize;
    new_file.offset = 0;
    new_file.access_mode = 0x01;

    opened_files = realloc(opened_files, (numOpenedFiles + 1) * sizeof(open_file));
    opened_files[numOpenedFiles] = new_file;
    numOpenedFiles++;
}

void closeFile(const char* filename) {
    //check to see if file exists
    loadDirectoryEntries(currentClusterNumber);
    directoryEntry* fileEntry = find_file_in_directory(filename);
    if (fileEntry == NULL) {
        printf("Error: File '%s' not found.\n", filename);
        return;
    }

    //check if file is open
    bool fileOpen = false;
    for (int i = 0; i < numOpenedFiles; ++i) {
        if (strcmp(opened_files[i].name, filename) == 0 && opened_files[i].is_open) {
            fileOpen = true;
            //remove the file entry from the open file list
            for (int j = i; j < numOpenedFiles - 1; ++j) {
                opened_files[j] = opened_files[j + 1];
            }
            numOpenedFiles--;
            break;
        }
    }

    if (!fileOpen) {
        printf("Error: File '%s' is not open.\n", filename);
    }

}

void read_data_from_file(const char *filename, int size) {
    open_file* file = NULL;

    for (int i = 0; i < numOpenedFiles; ++i) {
	if (strcmp(opened_files[i].name, filename) == 0 && opened_files[i].is_open) {
	    file = &opened_files[i];
	    break;
	}
    }

    if (file == NULL) {
	printf("Error: File '%s' is not open for reading.\n", filename);
	return;
    }

    if (file->access_mode != 0x01) {
	printf("Error: File '%s' is not opened for reading.\n", filename);
	return;
    }

    if (file->offset >= file->size) {
	printf("Error: Reached end of file.\n");
	return;
    }

    if (file->offset + size > file->size) {
	size = file->size - file->offset;
    }

    uint32_t cluster = file->start_cluster + (file->offset / (image->sectpClus * image->BpSect));
    uint32_t offset = convert_cluster_to_offset(cluster) + (file->offset % (image->sectpClus * 
			    image->BpSect));
    char* buffer = malloc(size);
    ssize_t bytes_read = pread(image->fd, buffer, size, offset);

    if (bytes_read != size) {
	printf("Error: Could not read bytes.\n");
	free(buffer);
	return;
    }

    printf("Data read from file '%s':\n", filename);
    for (int i = 0; i < size; ++i) {
	printf("%c", buffer[i]);
    }
    printf("\n");

    free(buffer);
    file->offset += bytes_read;
}

void write_data_to_file(const char *filename, const char *data) {
    open_file *file = NULL;
    for (int i = 0; i < numOpenedFiles; i++) {
	if (strcmp(opened_files[i].name, filename) == 0 && opened_files[i].is_open) {
	    file = &opened_files[i];
	    break;
	}
    }

    if (file == NULL) {
	printf("Error: File '%s' not found or not opened.\n", filename);
	return;
    }

    if (file->access_mode == 0x01) {
	printf("Error: File '%s' is not write accessible.\n", filename);
	return;
    }

    int dataLength = strlen(data);
    int remainingBytes = dataLength;
    int dataOffset = file->offset;
    int clusterSize = image->sectpClus * image->BpSect;

    uint32_t cluster = file->start_cluster + (dataOffset / clusterSize);
    uint32_t clusterOffset = convert_cluster_to_offset(cluster) + (dataOffset % clusterSize);

    while (remainingBytes > 0) {
	int spaceInCluster = clusterSize - (clusterOffset % clusterSize);
	int bytesToWrite = (remainingBytes < spaceInCluster) ? remainingBytes : spaceInCluster;

	ssize_t bytesWritten = pwrite(image->fd, data, bytesToWrite, clusterOffset);

	if (bytesWritten != bytesToWrite) {
	    printf("Error: Failed to write to file '%s'.\n", filename);
	    return;
	}

	remainingBytes -= bytesWritten;
	data += bytesWritten;
	clusterOffset += bytesWritten;

	if (clusterOffset % clusterSize == 0) {
	    uint32_t nextCluster = getNextCluster(cluster);
   	    
	    if (nextCluster == 0) {
		nextCluster = allocateNewCluster();
		if (nextCluster == 0) {
		    printf("Error: No more free clusters.\n");
    		    return;
		}
	    
	        setFATEntry(cluster, nextCluster);
	    }
	

	    cluster = nextCluster;
	    clusterOffset = convert_cluster_to_offset(cluster);
        }
    }

    file->offset += dataLength;
    printf("Data written to '%s'.\n", filename);
}

uint32_t compute_dentry_offset(uint32_t clusterNumber, const char* filename) {
        int fat32_fd = image->fd;
        uint32_t clusterSize = image->sectpClus * image->BpSect;
        uint32_t offset = image->dataStartOffset + (clusterNumber - 2) * clusterSize;
        uint32_t end_offset = image->dataStartOffset + (clusterNumber - 2) * clusterSize + clusterSize;

        while (offset + sizeof(directoryEntry) <= end_offset) {
            directoryEntry *entry = encode_dir_entry(fat32_fd, offset);
            if (compareDirectoryEntryName(entry->DIR_Name, filename)) {
		return offset;
            }
            offset += sizeof(directoryEntry);
        }
        printf("offset not calc");
        return 0;
}

bool removeFile(const char *filename) {
    //check if the file is open
    for (int i = 0; i < numOpenedFiles; ++i) {
        if (strcmp(opened_files[i].name, filename) == 0 && opened_files[i].is_open) {
            printf("Error: File '%s' is open. Please close.\n", filename);
            return false;
        }
    }
    directoryEntry* dentry = find_file_in_directory(filename);
    if (dentry == NULL) {
        printf("Error: File '%s' not found.\n", filename);
        return;
    }
    
    //get the offset of the entry and write the bytes
    uint32_t offset = compute_dentry_offset(currentClusterNumber, filename);
    dentry->DIR_Name[0] = 0x00;
    ssize_t bytes_written = pwrite(image->fd, dentry, sizeof(directoryEntry), offset);
    if (bytes_written != sizeof(directoryEntry)) {
        printf("Error: Failed to write directory entry.\n");
        return false;
    }

    return true;

}

bool makeDirectory(const char *dirname) {
    loadDirectoryEntries(currentClusterNumber);

    // Check if the directory already exists
    for (int i = 0; i < numDirectoryEntries; i++) {
        directoryEntry *entry = &directoryEntries[i];
        if (compareDirectoryEntryName(entry->DIR_Name, dirname) == 1) {
            printf("Error: Directory already exists.\n");
            return false;
        }
    }

    uint32_t newCluster = allocateNewCluster();
    if (newCluster == 0) {
        printf("Error: No free clusters.\n");
	return false;
    }
    
    uint32_t clusterSize = image->sectpClus * image->BpSect;
    uint32_t newClusterOffset = convert_cluster_to_offset(newCluster);

    directoryEntry dotEntry, dotDotEntry;
    memset(&dotEntry, 0, sizeof(directoryEntry));
    memset(&dotEntry, 0, sizeof(directoryEntry));

    strncpy(dotEntry.DIR_Name, ".          ", 11);
    dotEntry.DIR_Attr = ATTR_DIRECTORY;
    dotEntry.DIR_FstClusHI = (newCluster >> 16) & 0xFFFF;
    dotEntry.DIR_FstClusLO = newCluster & 0xFFFF;
    dotEntry.DIR_FileSize = 0;

    strncpy(dotDotEntry.DIR_Name, "..         ", 11);
    dotEntry.DIR_Attr = ATTR_DIRECTORY;
    dotEntry.DIR_FstClusHI = (currentClusterNumber >> 16) & 0xFFFF;
    dotEntry.DIR_FstClusLO = currentClusterNumber & 0xFFFF;
    dotEntry.DIR_FileSize = 0;

    ssize_t bytesWritten1 = pwrite(image->fd, &dotEntry, sizeof(directoryEntry), newClusterOffset);
    ssize_t bytesWritten2 = pwrite(image->fd, &dotDotEntry, sizeof(directoryEntry), 
		    newClusterOffset + sizeof(directoryEntry));

    if (bytesWritten1 != sizeof(directoryEntry) || bytesWritten2 != sizeof(directoryEntry)) {
	printf("Error initializing new directory.\n");
	return false;
    }

    uint32_t parentClusterOffset = image->dataStartOffset + (currentClusterNumber - 2) * clusterSize;    
    while (parentClusterOffset < image->dataStartOffset + (currentClusterNumber - 2 + 1) 
		    * clusterSize) {
        directoryEntry *entry = encode_dir_entry(image->fd, parentClusterOffset);
        if (entry->DIR_Name[0] == 0x00 || entry->DIR_Name[0] == 0xE5) {
            strncpy(entry->DIR_Name, dirname, 11);
            entry->DIR_Attr = ATTR_DIRECTORY;
            entry->DIR_FstClusHI = (newCluster >> 16) & 0xFFF;
            entry->DIR_FstClusLO = newCluster & 0xFFFF;
            entry->DIR_FileSize = 0;

            ssize_t bytesWritten = pwrite(image->fd, entry, sizeof(directoryEntry), 
				parentClusterOffset);
	    if (bytesWritten != sizeof(directoryEntry)) {
		printf("Error writing new directory entry.\n");
		return false;
	    }
		break;
	}
	parentClusterOffset += sizeof(directoryEntry);
    }

    return true;
}

bool createFile(const char *filename) {
    int fat32_fd = image->fd;
    uint32_t clusterSize = image->sectpClus * image->BpSect;
    
    for (int i = 0; i < numDirectoryEntries; i++) {
        struct directoryEntry *dentry = &directoryEntries[i];

        if (compareDirectoryEntryName(dentry->DIR_Name, filename) == 1) {
            printf("Error: File already exists.\n");
            return false;
        }
    }
    
    //iterate through the cluster chain to find available entries
    uint32_t clusterNumber = currentClusterNumber;
    while (clusterNumber >= 2) {
        uint32_t offset = image->dataStartOffset + (clusterNumber - 2) * clusterSize;
        uint32_t end_offset = image->dataStartOffset + (clusterNumber - 2) * clusterSize + clusterSize;

        //check if there is space in the current cluster
        while (offset + sizeof(directoryEntry) <= end_offset) {
            directoryEntry *entry = encode_dir_entry(fat32_fd, offset);

            if (entry->DIR_Name[0] == 0x00 || entry->DIR_Name[0] == 0xE5) {
                strncpy(entry->DIR_Name, filename, 11);
                entry->DIR_Attr = ATTR_ARCHIVE;
                entry->DIR_FstClusHI = (clusterNumber >> 16) & 0xFFF;
                entry->DIR_FstClusLO = clusterNumber & 0xFFFF;
                entry->DIR_FileSize = 0;

                ssize_t bytesWritten = pwrite(image->fd, entry, sizeof(directoryEntry), offset);
                return true;
            }
            offset += sizeof(directoryEntry);
        }

        //get next cluster in the chain
        clusterNumber = getNextCluster(clusterNumber);
    }
	
    //if no available space, allocate a new cluster and add the entry
    uint32_t newCluster = allocateNewCluster();

    if (newCluster == 0) {
        printf("Error: No free clusters.\n");
        return false;
    }

    loadDirectoryEntries(newCluster);

    for (int i = 0; i < numDirectoryEntries; i++) {
        struct directoryEntry *dentry = &directoryEntries[i];

        if (dentry->DIR_Name[0] == 0x00 || dentry->DIR_Name[0] == 0xE5) {
            strncpy(dentry->DIR_Name, filename, 11);
            dentry->DIR_Attr = ATTR_ARCHIVE;
            dentry->DIR_FstClusHI = (newCluster >> 16) & 0xFFF;
            dentry->DIR_FstClusLO = newCluster & 0xFFFF;
            dentry->DIR_FileSize = 0;

            uint32_t offset = convert_cluster_to_offset(newCluster) + (i * sizeof(directoryEntry));
            ssize_t bytesWritten = pwrite(image->fd, dentry, sizeof(directoryEntry), offset);

            return true;
        }
    }

    printf("Error: No free directory entries.\n");
    return false;
}


//assumes file descriptor is set
void initImage() {
    char buf0[2];
    ssize_t bytes_read0 = pread(image->fd, buf0, 2, 11);
    image->BpSect = buf0[0] + buf0[1] << 8;

    char buf1[1];
    ssize_t bytes_read1 = pread(image->fd, buf1, 1, 13);
    image->sectpClus = buf1[0];

    char buf2[2];
    ssize_t bytes_read2 = pread(image->fd, buf2, 2, 14);
    image->rsvSecCnt = (buf2[1] << 8) | buf2[0];
    int fatAddr = image->BpSect * image->rsvSecCnt;

    char buf3[1];
    ssize_t bytes_read3 = pread(image->fd, buf3, 1, 16);
    image->numFATs = buf3[0];

    char buf4[4];
    ssize_t bytes_read4 = pread(image->fd, buf4, 4, 36);
    image->secpFAT = (buf4[0] & 0xFF) + (buf4[1] << 8) + (buf4[2] << 16) + (buf4[3] << 24);

    char buf5[4];
    ssize_t bytes_read5 = pread(image->fd, buf5, 4, 44);
    image->rootClus = buf5[0] + (buf5[1] << 8) + (buf5[2] << 16) + (buf5[3] << 24);

    uint16_t totSec16;
    uint32_t totSec32;
    ssize_t bytes_read6 = pread(image->fd, &totSec16, 2, 19);
    ssize_t bytes_read7 = pread(image->fd, &totSec32, 4, 32);
    image->totalSec = totSec16 ? totSec16 : totSec32;

    uint32_t totalDataSec = image->totalSec - (image->rsvSecCnt + (image->numFATs * image->secpFAT));
    image->totalDataClus = totalDataSec / image->sectpClus;
    uint32_t fatSizeInBytes = image->secpFAT * image->BpSect;
    image->entpFAT = fatSizeInBytes / 4;
    
    image->dataStartOffset = image->BpSect * (image->rsvSecCnt + image->secpFAT * image->numFATs);
}

//info command
void printImageStruct() {
    printf("bytes per sector: %" PRIu16 "\n", image->BpSect);
    printf("sectors per cluster: %" PRIu8 "\n", image->sectpClus);
    printf("root cluster: %" PRIu32 "\n", image->rootClus);
    printf("total # of clusters in data region: %" PRIu32 "\n", image->totalDataClus);
    printf("# of entries in one FAT: %" PRIu32 "\n", image->entpFAT);
    printf("size of image (in bytes): %" PRId64 "\n", image->size);
}

//user input related functions
void displayPrompt(char *imageFileName, char *currentDirectory) {
	if (currentDirectory == NULL){
		currentDirectory = "";
	} 
    printf("%s/%s> ", imageFileName, currentDirectory);
}
char *get_input(void) {
    char *buffer = NULL;
    int bufsize = 0;
    char line[5];
    while (fgets(line, 5, stdin) != NULL) {
        int addby = 0;
        char *newln = strchr(line, '\n');
        if (newln != NULL)
            addby = newln - line;
        else
            addby = 5 - 1;
        buffer = (char *)realloc(buffer, bufsize + addby);
        memcpy(&buffer[bufsize], line, addby);
        bufsize += addby;
        if (newln != NULL)
            break;
    }
    buffer = (char *)realloc(buffer, bufsize + 1);
    buffer[bufsize] = 0;
    return buffer;
}

directoryEntry* find_file_in_directory(const char* filename) {
    for (int i = 0; i < numDirectoryEntries; ++i) {
	if (compareDirectoryEntryName(directoryEntries[i].DIR_Name, filename)) {
	    return &directoryEntries[i];
	}
    }
    return NULL;
}

bool is_directory_empty(uint32_t directoryCluster) {
    uint32_t offset = convert_cluster_to_offset(directoryCluster);
    directoryEntry entry;
    int entriesToCheck = image->BpSect * image->sectpClus / sizeof(directoryEntry);  // Calculate number of entries per cluster

    lseek(image->fd, offset, SEEK_SET);
    // We need to loop through the directory entries in the cluster
    for (int i = 0; i < entriesToCheck; i++) {
        // Read the next directory entry
        if(pread(image->fd, &entry, sizeof(directoryEntry), offset + i * sizeof(directoryEntry)) != sizeof(directoryEntry)) {
            printf("Failed to read directory entry\n");
            return false;
	}
        printf("Current directory entry being checked: %.11s\n", entry.DIR_Name);
        // Check if the entry is empty
        if (entry.DIR_Name[0] == 0x00) {
            return true;
        } else if (entry.DIR_Name[0] == 0xE5) {
            printf("Entry marked deleted\n");
            continue;
	} else if ( i<2 && (entry.DIR_Name[0] == '.')) {
            printf("Entry marked . or ..\n");
	    continue;
	}
	return false;
    }

    return true;
}

bool remove_empty_directory(uint32_t directoryCluster, char *path) {
    uint32_t targetCluster = findClusterInDirectory(directoryCluster, path);
    if(targetCluster == 0) {
        printf("Error locating directory\n");
	return false;
    }
    if(is_directory_empty(targetCluster)) {
        removeFile(path);
    } else {
        printf("Directory is not empty\n");
	return false;
    }
    return true;
}

tokenlist *new_tokenlist(void) {
    tokenlist *tokens = (tokenlist *)malloc(sizeof(tokenlist));
    tokens->size = 0;
    tokens->items = (char **)malloc(sizeof(char *));
    tokens->items[0] = NULL; /* make NULL terminated */
    return tokens;
}

void add_token(tokenlist *tokens, char *item) {
    int i = tokens->size;
    tokens->items = (char **)realloc(tokens->items, (i + 2) * sizeof(char *));
    tokens->items[i] = (char *)malloc(strlen(item) + 1);
    tokens->items[i + 1] = NULL;
    strcpy(tokens->items[i], item);
    tokens->size += 1;
}

tokenlist *get_tokens(char *input) {
    char *buf = (char *)malloc(strlen(input) + 1);
    strcpy(buf, input);
    tokenlist *tokens = new_tokenlist();
    char *tok = strtok(buf, " ");
    while (tok != NULL) {
        add_token(tokens, tok);
        tok = strtok(NULL, " ");
    }
    free(buf);
    return tokens;
}

void free_tokens(tokenlist *tokens) {
    for (int i = 0; i < tokens->size; i++)
        free(tokens->items[i]);
    free(tokens->items);
    free(tokens);
}
