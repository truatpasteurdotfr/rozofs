#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>


#define BUFFER_SIZE      (257*1024)
#define DEFAULT_FILENAME "essai_offset"
#define DEFAULT_BLOCK_SIZE 1

char FILENAME[500];

unsigned int offset;
unsigned int soffset;


char * pBuff = NULL;
char * pBlock = NULL;
int buffSize;
int blockSize = DEFAULT_BLOCK_SIZE;

static void usage() {
    printf("Parameters:\n");
    printf("[ -f <filename> ]    file name to do the test on (default %s)\n", DEFAULT_FILENAME);
    printf("[ -sz <blockSize> ]  size of the block to start the loop from (default %d)\n", DEFAULT_BLOCK_SIZE);
    printf("[ -o <offset> ]      offset to write the block in the file (1 shot)\n");
    printf("[ -s <offset> ]      offset to start a loop on every next offset (default is 0)\n");
    exit(0);
}

static void read_parameters(argc, argv)
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

int do_offset(int idx) {
    int f;
    ssize_t size;
    int idx2;
    int res = 1;

    remove(FILENAME);

    f = open(FILENAME, O_RDWR | O_CREAT, 0640);
    if (f == -1) {
        printf("Can not open %s\n", FILENAME);
        perror("open");
        return 0;
    }

    size = pwrite(f, pBlock, blockSize, idx);
    if (size != blockSize) {
        printf("Can not write %s at offset %d\n", FILENAME, idx);
        perror("pwrite");
        close(f);
        return 0;
    }

    f = close(f);
    if (f != 0) {
        printf("Can not close %s\n", FILENAME);
        perror("close");
        return 0;
    }

    f = open(FILENAME, O_RDONLY);
    if (f == -1) {
        printf("Can not re-open %s\n", FILENAME);
        perror("open");
        return 0;
    }

    memset(pBuff, 0, buffSize);
    size = pread(f, pBuff, buffSize, 0);
    if (size != (idx + blockSize)) {
        printf("pread only %"PRIu64" while expecting %d\n",
                (uint64_t) size, (idx + blockSize));
        perror("pread");
        close(f);
        return 0;
    }
    for (idx2 = 0; idx2 < idx; idx2++) {
        if (pBuff[idx2] != 0) {
            printf("loop %d : offset %d contains %x\n", idx, idx2, pBuff[idx2]);
            res = 0;
        }
    }
    if (strncmp(pBlock, &pBuff[idx], blockSize) != 0) {
        printf("loop %d : string contains ", idx);
        for (idx2 = 0; idx2 < blockSize; idx2++) printf("%2.2x ", pBuff[idx + idx2]);
        printf("at offset %d\n", idx);
        res = 0;
    }

    f = close(f);
    if (f != 0) {
        perror("close");
    }
    return res;
}

int main(int argc, char **argv) {
    read_parameters(argc, argv);
    int idx;
    unsigned char c;

    buffSize = 2 * BUFFER_SIZE + 1;


    pBuff = malloc(buffSize);
    if (pBuff == NULL) {
        printf("Can not allocate %d bytes\n", buffSize);
        perror("malloc");
        return -1;
    }
    pBlock = malloc(BUFFER_SIZE + 1);
    c = 'A';
    for (idx = 0; idx < BUFFER_SIZE; idx++) {
        pBlock[idx] = c;
        if (c == 'Z') c = 'A';
        else c++;
    }
    pBlock[idx] = 0;


    /*
     * One shot test . Write a given block of a given size
     * at a given offset and then re-read
     */
    if (offset != -1) {
        if (do_offset(offset) == 1) {
            printf("Succesfull trial at offset on %d\n", offset);
        }
    }        /*
  * Loop on every size and every offset
  */
    else while (blockSize < BUFFER_SIZE) {

            offset = soffset;
            soffset = 0;

            while (offset < BUFFER_SIZE) {

                /* Try until success */
                while (do_offset(idx) == 0) {
                    printf("ERROR !!! blocksize %6d  - offset %6d\n", blockSize, offset);
                    sleep(1);
                }

                if ((offset % 1000) == 0) {
                    printf("blocksize %6d  - offset %6d\n", blockSize, offset);
                }
                offset++;
            }
            blockSize++;
        }
    free(pBuff);

    exit(0);

}
