#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>


#define BUFFER_SIZE      (1024*2+1)
#define DEFAULT_FILENAME "test_create_write_read_random"
#define DEFAULT_BLOCK_SIZE 1
#define DEFAULT_NB_PROCESS 4
char FILENAME[500];

#define ERRNO(s) 

char * pBuff = NULL;
char * pBlock = NULL;
int buffSize;
int nbProcess = DEFAULT_NB_PROCESS;
int myProcId;

static void usage() {
    printf("Parameters:\n");
    printf("[ -file <name> ]   The create/write/read/delete test will be done on <name>.<procNumber> (default %s)\n", DEFAULT_FILENAME);
    printf("[ -process <nb> ]  The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    exit(0);
}

static void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;

    strcpy(FILENAME, DEFAULT_FILENAME);

    idx = 1;
    while (idx < argc) {

        /* -file <name> */
        if (strcmp(argv[idx], "-file") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%s", FILENAME);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }
	
        /* -process <nb>  */
        if (strcmp(argv[idx], "-process") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%u", &nbProcess);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }
	
        printf("Unexpected parameter %s\n", argv[idx]);
        usage();
    }
}

int do_offset(char * filename, int offset, int blockSize) {
    int f;
    ssize_t size;
    int idx2;
    int res = 1;

    remove(filename);

    f = open(filename, O_RDWR | O_CREAT, 0640);
    if (f == -1) {
        printf("proc %d - open %d\n",myProcId, errno);
        printf("proc %d - Can not open %s\n",myProcId, filename);
        return 0;
    }

    size = pwrite(f, pBlock, blockSize, offset);
    if (size != blockSize) {
        printf("proc %d - pwrite %d\n",myProcId,errno);
        printf("proc %d - Can not write %s size %d at offset %d\n", myProcId, filename, blockSize, offset);
        close(f);
        return 0;
    }

    f = close(f);
    if (f != 0) {
        printf("proc %d - close %d\n",myProcId,errno);
        printf("proc %d - Can not close %s\n", myProcId, filename);
        return 0;
    }

    f = open(filename, O_RDONLY);
    if (f == -1) {
        printf("proc %d - re-open %d\n",myProcId,errno);
        printf("proc %d - Can not re-open %s\n", myProcId,filename);
        return 0;
    }

    memset(pBuff, 0, buffSize);
    size = pread(f, pBuff, buffSize, 0);
    if (size != (offset + blockSize)) {
        printf("proc %d - pread %d\n",myProcId,errno);    
        printf("proc %d - pread only %u while expecting %d\n", myProcId, size, (offset + blockSize));
        close(f);
        return 0;
    }
    for (idx2 = 0; idx2 < offset; idx2++) {
        if (pBuff[idx2] != 0) {
            printf("proc %d - offset %d contains %x instead of 0\n", myProcId, idx2, pBuff[idx2]);
            res = 0;
        }
    }
    for (idx2=0; idx2 < blockSize; idx2++) {
        if (pBuff[idx2+offset] != pBlock[idx2]) {
            printf("proc %d - offset %d + %d contains %x instead of %x\n", myProcId, idx2, offset, pBuff[idx2+offset],pBlock[idx2]);
            res = 0;
	    break;
        }
    }    

    f = close(f);
    if (f != 0) {
        printf("proc %d - close %d\n",myProcId,errno);
        printf("proc %d - Can not close %s\n", myProcId, filename);
    }
    return res;
}

void loop_test_process() {
  unsigned int blockSize;
  unsigned int offset;
  int count=0;    
  char filename[500];
  
  sprintf(filename,"%s.%d",FILENAME, myProcId);
    
  while (1) {

    offset    = random() % BUFFER_SIZE; 
    blockSize = random() % BUFFER_SIZE;
    if (blockSize == 0) blockSize = 1;
    
    if (do_offset(filename,offset,blockSize) == 0) {
      printf("proc %d - ERROR !!! blocksize %6d  - offset %6d\n", myProcId, blockSize, offset);
      sleep(4);
    } 
     
    count++;
    if ((count % 1000)==0) printf("proc %d - Loop %d\n",myProcId, count);
  }
  exit(0);
}  
int main(int argc, char **argv) {

  int idx;
  unsigned char c;
  pid_t pid;
  int proc;
  
  read_parameters(argc, argv);

  if (nbProcess <= 0) {
    printf("Bad -process option %d\n",nbProcess);
    exit(0);
  }
    
  
  // Prepare a working buffer and initalize its content
  buffSize = 2 * BUFFER_SIZE + 1;
  pBuff = malloc(buffSize);
  if (pBuff == NULL) {
      printf("Can not allocate %d bytes\n", buffSize);
      perror("malloc");
      return -1;
  }
  pBlock = malloc(BUFFER_SIZE + 1);
  c = 'A';
  for (idx = 0; idx < buffSize; idx++) {
      pBlock[idx] = c;
      if (c == 'Z') c = 'A';
      else c++;
  }
  pBlock[idx] = 0;


  for (proc=0; proc < nbProcess; proc++) {
  
     pid = fork();     
     if (pid == 0) {
       myProcId = proc;
       loop_test_process();
     }  
  }

  while (1) {
    sleep(5);
  }
  
  free(pBuff);
  exit(0);
}
