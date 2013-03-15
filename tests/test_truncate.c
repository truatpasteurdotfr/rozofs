#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>


#define BUFFER_SIZE      (257*1024)
#define DEFAULT_FILENAME "essai_offset"
#define DEFAULT_BLOCK_SIZE (9*512)

char FILENAME[500];

unsigned int offset;
unsigned int soffset;


char * pBuff = NULL;
char * pBlock = NULL;
int buffSize;
int blockSize = DEFAULT_BLOCK_SIZE;

int usage() {
    printf("Parameters:\n");
    printf("[ -f <filename> ]    file name to do the test on (default %s)\n", DEFAULT_FILENAME);
    printf("[ -sz <blockSize> ]  size of the block to start the loop from (default %d)\n", DEFAULT_BLOCK_SIZE);
    //  printf("[ -o <offset> ]      offset to write the block in the file (1 shot)\n");
    //  printf("[ -s <offset> ]      offset to start a loop on every next offset (default is 0)\n");
    exit(0);
}

void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;

    strcpy(FILENAME, DEFAULT_FILENAME);
    offset = -1;
    soffset = 0;

    idx = 1;
    while (idx < argc) {

        /* -f <file> */
        if (strcmp(argv[idx], "-f") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx]);
                usage();
            }
            ret = sscanf(argv[idx], "%s", FILENAME);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx - 1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }
        /* -sz <blockSize> */
        if (strcmp(argv[idx], "-sz") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx]);
                usage();
            }
            ret = sscanf(argv[idx], "%d", &blockSize);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx - 1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }
        /* -o <offset> */
        if (strcmp(argv[idx], "-o") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx]);
                usage();
            }
            ret = sscanf(argv[idx], "%d", &offset);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx - 1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }

        /* -s <start offset> */
        if (strcmp(argv[idx], "-s") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx]);
                usage();
            }
            ret = sscanf(argv[idx], "%d", &soffset);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx - 1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }

        printf("Unexpected parameter %s\n", argv[idx]);
        usage();
    }
}

int main(int argc, char **argv) {
    read_parameters(argc, argv);
    int ret = 0;

    ret = truncate(FILENAME, (off_t) blockSize);
    if (ret < 0) {
        printf("Truncate failure %s\n", strerror(errno));
    }

    exit(0);
}