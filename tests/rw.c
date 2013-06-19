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

#define RANDOM_BLOCK_SIZE      (9*1024)
#define RANDOM_BLOCK_OFFSET    (270*1024*20)
#define READ_BUFFER_SIZE       (RANDOM_BLOCK_OFFSET+RANDOM_BLOCK_SIZE)

#define DEFAULT_FILENAME "mnt1/this_is_the_default_rw_test_file_name"
#define DEFAULT_NB_PROCESS 100
#define DEFAULT_LOOP       1000


char FILENAME[500];

char * pReadBuff    = NULL;
char * pCompareBuff = NULL;
char * pBlock       = NULL;
int nbProcess       = DEFAULT_NB_PROCESS;
int myProcId;
int loop=DEFAULT_LOOP;
int with_close = 1;
int * result;

static void usage() {
    printf("Parameters:\n");
    printf("[ -file <name> ]   file to do the test on (default %s)\n", DEFAULT_FILENAME);
    printf("[ -process <nb> ]  The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    printf("[ -loop <nb> ]     <nb> test operations will be done (default %d)\n",DEFAULT_LOOP);
    printf("[ -noclose ]       Do not close the file between write and read\n");
    exit(-100);
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
        /* -noclose   */
        if (strcmp(argv[idx], "-noclose") == 0) {
            idx++;
            with_close = 0;
            continue;
        }		
        printf("Unexpected parameter %s\n", argv[idx]);
        usage();
    }
}


int do_write_offset(int f, int offset, int blockSize) {
    ssize_t size;

    memcpy(&pCompareBuff[offset],pBlock,blockSize);
//    printf("PWRITE off [%x,%x] size %d\n",offset,offset+blockSize,blockSize);
    size = pwrite(f, pBlock, blockSize, offset);
    if (size != blockSize) {
        printf("proc %3d - pwrite %d\n",myProcId,errno);
        printf("proc %3d - Can not write %s size at offset %d\n", myProcId, blockSize, offset);
        return 0;
    }
    return 1;

}
int do_read_and_check(int f) {
    ssize_t size;
    int idx2;
    int res = 1;
    size = pread(f, pReadBuff, READ_BUFFER_SIZE, 0);
    if (size <= 0) {
        printf("proc %3d - pread %d\n",myProcId,errno);
        printf("proc %3d - Can not read size %d\n", myProcId, READ_BUFFER_SIZE);    
        return 0;
    }
//    printf("PREAD size = %d (%x)\n",size,size);
    for (idx2 = 0; idx2 < READ_BUFFER_SIZE; idx2++) {
        if (pReadBuff[idx2] != pCompareBuff[idx2]) {
            printf("\nproc %3d - offset %d = 0x%x contains %x instead of %x\n", myProcId, idx2, idx2, pReadBuff[idx2],pCompareBuff[idx2]);
	    printf("proc %3d - file (%d has been read)\n", myProcId,size);
            hexdump(pReadBuff,idx2-16, 64);	    
	    printf("proc %3d - ref buf\n", myProcId);
            hexdump(pCompareBuff,idx2-16, 64); 
            return 0;
        }
    }
    
    return 1;
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
        return 0;
    }

    nbWrite = 1 + count % 3;
    
    while (nbWrite--) {

      offset    = (random()+offset)    % RANDOM_BLOCK_OFFSET; 
      blockSize = (random()+blockSize) % RANDOM_BLOCK_SIZE;      
      if (blockSize == 0) blockSize = 1;

      if (do_write_offset(f, offset, blockSize) == 0) {
	printf("proc %3d - ERROR !!! do_write_offset blocksize %6d  - offset %6d\n", myProcId, blockSize, offset);
	close(f);      
	return 0;
      }  
    }
        
    if (with_close) {
      f = close(f);
      if (f != 0) {
          printf("proc %3d - close %d\n",myProcId,errno);
          printf("proc %3d - Can not close %s\n", myProcId, filename);
          return 0;
      }

      f = open(filename, O_RDONLY);
      if (f == -1) {
          printf("proc %3d - re-open %d\n",myProcId,errno);
          printf("proc %3d - Can not re-open %s\n", myProcId,filename);
          return 0;
      }
    }
    
    if (do_read_and_check(f) == 0) {
      close(f);      
      return 0;
    }
    
    f = close(f);
    if (f != 0) {
        printf("proc %3d - close %d\n",myProcId,errno);
        printf("proc %3d - Can not close %s\n", myProcId, filename);
    }
    return 1;
}
int read_empty_file(char * filename) {
    int f;
    ssize_t size;
    int idx,idx2;
    int res = 1;


    f = open(filename, O_RDWR | O_CREAT, 0640);
    if (f == -1) {
        printf("proc %3d - open %d\n",myProcId, errno);
        printf("proc %3d - Can not open %s\n",myProcId, filename);
        return 0;
    }


    size = pread(f, pReadBuff, READ_BUFFER_SIZE, 0);
    if (size < 0) {
        printf("proc %3d - pread %d\n",myProcId,errno);
        printf("proc %3d - Can not read %s size %d\n", myProcId, filename, READ_BUFFER_SIZE);    
        close(f);
        return 0;
    }
    
    f = close(f);
    if (f != 0) {
        printf("proc %3d - close %d\n",myProcId,errno);
        printf("proc %3d - Can not close %s\n", myProcId, filename);
    }
    return res;
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
      return 0;
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
    
  // Prepare a working buffer to read from or write to the file
  pReadBuff = NULL;
  pReadBuff = malloc(READ_BUFFER_SIZE);
  if (pReadBuff == NULL) {
      printf("Can not allocate %d bytes\n", READ_BUFFER_SIZE);
      perror("malloc");
      return 0;
  }
  memset(pReadBuff,0,READ_BUFFER_SIZE);

  // Buffer containing what the file should contain
  pCompareBuff = NULL;
  pCompareBuff = malloc(READ_BUFFER_SIZE);
  if (pCompareBuff == NULL) {
      printf("Can not allocate %d bytes\n", READ_BUFFER_SIZE);
      perror("malloc");
      return 0;
  }
  memset(pCompareBuff,0,READ_BUFFER_SIZE);  
    
  sprintf(filename,"%s.%d",FILENAME, myProcId);
  if (unlink(filename) == -1) {
    if (errno != ENOENT) {
      printf("proc %3d - ERROR !!! can not remove %s %s\n", filename, strerror(errno));
      return 0;
    }
  }
  
#if 0  
  if (read_empty_file(filename) == 0) {
    printf("proc %3d - ERROR !!! read_empty_file\n", myProcId);
    return 0;
  }
  memset(pReadBuff,0,READ_BUFFER_SIZE);
#endif
          
  while (1) {
    count++;    
    if  (do_one_test(filename,count) == 0) {
      printf("proc %3d - ERROR in loop %d\n", myProcId, count);      
      return 0;
    } 
    if (loop==count) return 1;
  }
}  
int free_result(void) {
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
  char cmd[128];
    
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
    if (result[proc] == 0) {
      ret--;
    }
  }
  free_result();
  printf("OK %d / FAILURE %d\n",nbProcess+ret, -ret);
  exit(ret);
}
