#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

void displayPrompt();
char *get_input(void);
tokenlist *new_tokenlist(void);
void add_token(tokenlist *tokens, char *item);
tokenlist *get_tokens(char *input);
void free_tokens(tokenlist *tokens);

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
    uint32_t entpFat;
    int size;
}



int main(int argc, char *argv[]) {
	//checking for right arguments
	if (argc != 2) {
		printf("Argument error: ./filesys <FAT32 image file>\n");
		return 1;
	}
	
	int status;
	status = system("sudo mount -o loop %s ./mnt", argv[1]);
	if(status == -1) {
		perror("mount failed");
		return 1;
	}

    imageStruct* image = malloc(sizeof(imageStruct));
    image->fd = open(argv[1], 0_RDWR)


	while (1) {
		displayPrompt();
		char *input = get_input();
		tokenlist *tokens = get_tokens(input);
		
		//execution
		
		free(input);
		free_tokens(tokens);
	}

    return 0;
}


//assumes file descriptor is set
void initImage(*imageStruct image) {
    char buf0[2];
    ssize_t bytes_read0 = pread(image->fd, buf0, 2, 11);
    image->BpSect = buf0[0] + buf0[1] << 8;

    char buf1[1];
    ssize_t bytes_read1 = pread(image->fd, buf1, 1, 13);
    image->sectpClus = buf1[0] << 8;

    char buf2[2];
    ssize_t bytes_read2 = pread(image->fd, buf2, 2, 14);
    image->rsvSecCnt = buf2[0] + buf2[1] << 8;

    int fatAddr = image->BpSect * image->rsvSecCnt;

    char buf3[1];
    ssize_t bytes_read3 = pread(image->fd, buf3, 1, 16);
    image.numFATs = buf3[0];

    char buf4[4];
    ssize_t bytes_read4 = pread(image->fd, buf4, 4, 36);
    image.secpFAT = buf4[0] + (buf4[1] << 8) + (buf4[2] << 16) + (buf4[3] << 24);

    char buf5[4];
    ssize_t bytes_read5 = pread(image->fd, buf5, 4, 44);
    image.rootCluster = buf5[0] + (buf5[1] << 8) + (buf5[2] << 16) + (buf5[3] << 24);

    uint16_t totSec16;
    uint32_t totSec32;
    ssize_t bytes_read6 = pread(image->fd, &totSec16, sizeof(totSec16), 19);
    ssize_t bytes_read7 = pread(image->fd, &totSec32, sizeof(totSec32), 32);
    image->totalSec = totSec16 ? totSec16 : totSec32;

    uint32_t totalDataSec = image.totalSec - (image.rsvSecCnt + (image.numFATs * image.secpFAT));
    image.totalDataClus = totalDataSec / image.sectpClus;
}


//user input related functions

void displayPrompt(){
    char * user = getenv("USER");
    char * machine = getenv("MACHINE");
    char pwd[512];
    getcwd(pwd, sizeof(pwd));
    printf("%s@%s:%s>", user, machine, pwd);
}

char *get_input(void) {
    char *buffer = NULL;
    int bufsize = 0;
    char line[5];
    while (fgets(line, 5, stdin) != NULL)
    {
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
    while (tok != NULL)
    {
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
