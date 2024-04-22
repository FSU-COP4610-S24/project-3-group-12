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
char* getDirectoryNameByCluster(uint32_t parentCluster, uint32_t targetCluster);
bool changeDirectory(const char *dirname);
uint32_t getClusterNumber(char *path);
uint32_t findClusterInDirectory(uint32_t directoryCluster, char *name);

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

struct imageStruct *image;
struct directoryEntry *directoryEntries = NULL;
int numDirectoryEntries = 0;

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

    if (name[0] == 0xE5 || name[0] == 0x00 || name[0] == 0x20) {
        return 0; 
    }
    
    //check for other illegal characters
    for (int i = 0; i < 11; i++) {
        if (name[i] < 0x20 || name[i] == 0x22 || name[i] == 0x2A || name[i] == 0x2B || name[i] == 0x2C || 
            name[i] == 0x2F || name[i] == 0x3A || name[i] == 0x3B || name[i] == 0x3C || name[i] == 0x3D || name[i] == 0x3E || 
            name[i] == 0x3F || name[i] == 0x5B || name[i] == 0x5C || name[i] == 0x5D || name[i] == 0x7C) {
            return 0;
        }
    }
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
    printf("Printing computed cluster number: %d\n", clusterNumber);

    //reset global list of entries
    if (directoryEntries != NULL) {
        free(directoryEntries);
        directoryEntries = NULL;
    }
    numDirectoryEntries = 0;

    //read directory entries from the current cluster and its subsequent clusters
    while (clusterNumber >= 2 && clusterNumber <= 0x0FFFFFFF) {
        uint32_t offset = image->dataStartOffset + (clusterNumber - 2) * image->sectpClus * image->BpSect;
        printf("Printing offset: %02x\n", offset);

        //read directory entries from the current cluster
        while (1) {
            directoryEntry *entry = encode_dir_entry(fat32_fd, offset);
            if (entry == NULL || entry->DIR_Name[0] == 0x00) {
                break;
            }
            if (entry->DIR_Attr == 0x0F) { // Skip long file entries
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

        printf("Before getting next cluster\n");
        //get next cluster in the chain
        clusterNumber = getNextCluster(clusterNumber);
        printf("After getting next cluster: %d\n", clusterNumber);
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
	printf("%s\n", name);
    }
    
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
