#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <ctype.h>
#include <sys/wait.h>





#define DEFAULT_FILENAME    "mnt1/this_is_the_default_rw_test_file_name"
#define DEFAULT_NB_PROCESS    20
#define DEFAULT_LOOP         200
#define DEFAULT_FILE_SIZE_MB   1

#define RANDOM_BLOCK_SIZE      (9*1024)
#define READ_BUFFER_SIZE       (RANDOM_BLOCK_SIZE+file_mb)


int shmid;
#define SHARE_MEM_NB 7538

#define HEXDUMP_COLS 16
void hexdump(void *mem, unsigned int offset, unsigned int len)
{
        unsigned int i, j;
        
        for(i = 0; i < len + ((len % HEXDUMP_COLS) ? (HEXDUMP_COLS - len % HEXDUMP_COLS) : 0); i++)
        {
                /* print offset */
                if(i % HEXDUMP_COLS == 0)
                {
                        printf("0x%06x: ", i+offset);
                }
 
                /* print hex data */
                if(i < len)
                {
                        printf("%02x ", 0xFF & ((char*)mem)[i+offset]);
                }
                else /* end of block, just aligning for ASCII dump */
                {
                        printf("   ");
                }
                
                /* print ASCII dump */
                if(i % HEXDUMP_COLS == (HEXDUMP_COLS - 1))
                {
                        for(j = i - (HEXDUMP_COLS - 1); j <= i; j++)
                        {
                                if(j >= len) /* end of block, not really printing */
                                {
                                        putchar(' ');
                                }
                                else if(isprint(((char*)mem)[j+offset])) /* printable char */
                                {
                                        putchar(0xFF & ((char*)mem)[j+offset]);        
                                }
                                else /* other char */
                                {
                                        putchar('.');
                                }
                        }
                        putchar('\n');
                }
        }
}


char FILENAME[500];

char * pReadBuff    = NULL;
char * pCompareBuff = NULL;
char * pBlock       = NULL;
int nbProcess       = DEFAULT_NB_PROCESS;
int myProcId;
int loop=DEFAULT_LOOP;
long long unsigned int file_mb=DEFAULT_FILE_SIZE_MB*1000000;

int nodelete = 0;
int * result;

static void usage() {
    printf("Parameters:\n");
    printf("[ -file <name> ]   file to do the test on (default %s)\n", DEFAULT_FILENAME);
    printf("[ -process <nb> ]  The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    printf("[ -loop <nb> ]     <nb> test operations will be done (default %d)\n",DEFAULT_LOOP);
    printf("[ -fileSize <MB> ] file size in MB (default %d)\n",DEFAULT_FILE_SIZE_MB);
    printf("[ -nodelete ]      do not delete file after test\n");
    exit(-100);
}

static void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;
    int val;

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
        /* -nodelete  */
        if (strcmp(argv[idx], "-nodelete") == 0) {
            idx++;
            nodelete = 1;
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
	
        /* -loop <nb>  */
        if (strcmp(argv[idx], "-loop") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%u", &loop);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
                usage();
            }
            idx++;
            continue;
        }	
	
	/* -fileSize <MB> */
        if (strcmp(argv[idx], "-fileSize") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%u", &val);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
                usage();
            }
	    file_mb = val;
	    file_mb *= 1000000;
            idx++;
            continue;
        }	
			
        printf("Unexpected parameter %s\n", argv[idx]);
        usage();
    }
}


int do_write_offset(int f, int offset, int blockSize) {
    ssize_t size;

    size = pwrite(f, pBlock, blockSize, offset);
    if (size != blockSize) {
        printf("proc %3d - pwrite %d\n",myProcId,errno);
        printf("proc %3d - Can not write %d size at offset %d\n", myProcId, blockSize, offset);
        return -1;
    }
    return 0;

}
int do_one_test(char * filename, int count) {
    int f;
    unsigned int blockSize;
    unsigned int offset;
    unsigned int nbWrite;
    

    f = open(filename, O_RDWR | O_CREAT, 0640);
    if (f == -1) {
        printf("proc %3d - open %d\n",myProcId, errno);
        printf("proc %3d - Can not open %s\n",myProcId, filename);
        return -1;
    }

    nbWrite = 5 + count % 5;
    
    while (nbWrite--) {

      offset    = (random()+offset)    % file_mb; 
      blockSize = (random()+blockSize) % RANDOM_BLOCK_SIZE;      
      if (blockSize == 0) blockSize = 1;

      if (do_write_offset(f, offset, blockSize) != 0) {
	printf("proc %3d - ERROR !!! do_write_offset blocksize %6d  - offset %6d\n", myProcId, blockSize, offset);
	close(f);      
	return -1;
      }  
    }
        
    f = close(f);
    return 0;
}
int loop_test_process() {
  int count=0; 
  int idx;   
  char filename[500];
  char c;
  
  pBlock = NULL;
  pBlock = malloc(RANDOM_BLOCK_SIZE + 1);
  if (pBlock == NULL) {
      printf("Can not allocate %d bytes\n", RANDOM_BLOCK_SIZE+1);
      perror("malloc");
      return -1;
  }  
  c = 'A';
  int mark=0;
  for (idx = 0; idx < RANDOM_BLOCK_SIZE; idx++) {
      pBlock[idx] = c;
      if      (c == 'Z'){
        idx++;
	pBlock[idx] = mark++;
        c = 'a';
      }	
      else if (c == 'z') {
        idx++;
	pBlock[idx] = mark++;
        c = 'A';
      }	
      else c++;
  }
  pBlock[RANDOM_BLOCK_SIZE] = 0;
  sprintf(filename,"%s.%d",FILENAME, getpid());   
  unlink(filename);         
  while (1) {
    count++;    
    if  (do_one_test(filename,count) != 0) {
      printf("proc %3d - ERROR in loop %d\n", myProcId, count);      
      return -1;
    } 
    if (loop==count) return 0;
  }
}  
void free_result(void) {
  struct shmid_ds   ds;
  shmctl(shmid,IPC_RMID,&ds); 
}
int * allocate_result(int size) {
  struct shmid_ds   ds;
  void            * p;
      
  /*
  ** Remove the block when it already exists 
  */
  shmid = shmget(SHARE_MEM_NB,1,0666);
  if (shmid >= 0) {
    shmctl(shmid,IPC_RMID,&ds);
  }
  
  /* 
  * Allocate a block 
  */
  shmid = shmget(SHARE_MEM_NB, size, IPC_CREAT | 0666);
  if (shmid < 0) {
    perror("shmget(IPC_CREAT)");
    return 0;
  }  

  /*
  * Map it on memory
  */  
  p = shmat(shmid,0,0);
  if (p == 0) {
    shmctl(shmid,IPC_RMID,&ds);  
       
  }
  memset(p,0,size);  
  return (int *) p;
}
int main(int argc, char **argv) {
  pid_t pid[2000];
  int proc;
  int ret;
    
  read_parameters(argc, argv);

  if (nbProcess <= 0) {
    printf("Bad -process option %d\n",nbProcess);
    exit(-100);
  }

  result = allocate_result(4*nbProcess);
  if (result == NULL) {
    printf(" allocate_result error\n");
    exit(-100);
  }  
  for (proc=0; proc < nbProcess; proc++) {
  
     pid[proc] = fork();     
     if (pid[proc] == 0) {
       myProcId = proc;
       result[proc] = loop_test_process();
       exit(0);
     }  
  }

  for (proc=0; proc < nbProcess; proc++) {
    waitpid(pid[proc],NULL,0);        
  }
  
  ret = 0;
  for (proc=0; proc < nbProcess; proc++) {
    if (result[proc] != 0) {
      ret--;
    }
  }
  free_result();
  printf("OK %d / FAILURE %d\n",nbProcess+ret, -ret);
  exit(ret);
}
