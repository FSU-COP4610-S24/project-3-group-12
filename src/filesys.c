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

void displayPrompt();
char *get_input(void);
tokenlist *new_tokenlist(void);
void add_token(tokenlist *tokens, char *item);
tokenlist *get_tokens(char *input);
void free_tokens(tokenlist *tokens);
uint32_t findClusterInDirectory(uint32_t directoryCluster, char *name);

#define BYTES_PER_SECTOR 512
#define DIR_ENTRY_SIZE 32
#define ROOT_CLUSTER 2

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
        displayPrompt();
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

        free(input);
        free_tokens(tokens);
    }
    

    return 0;
}

uint32_t getClusterNumber(char *path){

	uint32_t currentCluster = image->rootClus;
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
        	return;
        }
    //iterate through directory entries to find the matching one

    for (int i = 0; i < numDirectoryEntries; ++i) {
        if (strcmp(directoryEntries[i].DIR_Name, name) == 0) {
            if (directoryEntries[i].DIR_Attr == ATTR_DIRECTORY) {
                return directoryEntries[i].DIR_FstClusHI;
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
    //check if the first character
    if (name[0] == 0xE5 || name[0] == 0x00 || name[0] == 0x20) {
        return 0; // Invalid name
    }
    
    //check for other illegal chars
    for (int i = 0; i < 11; i++) {
        if (name[i] < 0x20 || name[i] == 0x22 || name[i] == 0x2A || name[i] == 0x2B || name[i] == 0x2C || name[i] == 0x2E || 
            name[i] == 0x2F || name[i] == 0x3A || name[i] == 0x3B || name[i] == 0x3C || name[i] == 0x3D || name[i] == 0x3E || 
            name[i] == 0x3F || name[i] == 0x5B || name[i] == 0x5C || name[i] == 0x5D || name[i] == 0x7C) {
            return 0;
        }
    }
    
    return 1;
}

void loadDirectoryEntries(uint32_t clusterNumber) {
    int fat32_fd = image->fd;
    uint32_t offset = image->dataStartOffset + (clusterNumber - 2) * image->BpSect;

    //clear existing directory entries if any
    if (directoryEntries != NULL) {
        free(directoryEntries);
        directoryEntries = NULL;
    }

    //read directory entries from the current cluster
	while (1) {
	    directoryEntry *entry = encode_dir_entry(fat32_fd, offset);
	    if (entry == NULL || entry->DIR_Name[0] == 0x00) {
		break; // End of directory entries
	    }

	    // Check if the entry is a valid file or directory name
	    if (is_valid_name(entry->DIR_Name)) {
		// Allocate memory for directoryEntries if it's NULL
		if (directoryEntries == NULL) {
		    directoryEntries = malloc(sizeof(directoryEntry));
		    if (directoryEntries == NULL) {
		        perror("Error allocating memory for directory entries");
		        return;
		    }
		} else {
		    // Reallocate memory for directoryEntries
		    directoryEntry *temp = realloc(directoryEntries, (numDirectoryEntries + 1) * sizeof(directoryEntry));
		    if (temp == NULL) {
		        perror("Error reallocating memory for directory entries");
		        return;
		    }
		    directoryEntries = temp;
		}
		
		// Copy the entry to directoryEntries
		directoryEntries[numDirectoryEntries++] = *entry;
	    }
	    offset += sizeof(directoryEntry);
	}

}

void listDirectoryEntries(const char *path) {
    //compute the cluster number for the specified directory path
    uint32_t currentCluster = getClusterNumber(path);
    if (currentCluster == 0) {
        printf("Error: Unable to compute cluster number for path %s\n", path);
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

    //reset global list
    free(directoryEntries);
    directoryEntries = NULL;
    numDirectoryEntries = 0;
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
void displayPrompt() {
    char *user = getenv("USER");
    char *machine = getenv("MACHINE");
    char pwd[512];
    getcwd(pwd, sizeof(pwd));
    printf("%s@%s:%s>", user, machine, pwd);
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
