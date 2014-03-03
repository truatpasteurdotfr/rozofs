/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation, version 2.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
 */
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


#define DEFAULT_FILENAME    "mnt1_1/this_is_the_default_rw_test_file_name"
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

typedef enum _CHECK_MODE_E {
  CHECK_MODE_TOTAL,
  CHECK_MODE_PARTIAL,
  CHECK_MODE_RANDOM
}  CHECK_MODE_E;
CHECK_MODE_E  ckeck_mode = CHECK_MODE_TOTAL;  

long long unsigned int file_mb=DEFAULT_FILE_SIZE_MB*1000000;

int closeBetween = 0;
int closeAfter = 0;
int * result;

static void usage() {
    printf("Parameters:\n");
    printf("[ -file <name> ]           file to do the test on (default %s)\n", DEFAULT_FILENAME);
    printf("[ -process <nb> ]          The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    printf("[ -loop <nb> ]             <nb> test operations will be done (default %d)\n",DEFAULT_LOOP);
    printf("[ -fileSize <MB> ]         file size in MB (default %d)\n",DEFAULT_FILE_SIZE_MB);
    printf("[ -closeAfter ]            Close the file after each loop\n");
    printf("[ -closeBetween ]          Close the file between write and read\n");
    printf("[-total|-partial|-random]  Re-read and check all the file, only the written part or random parts\n");
    exit(-100);
}

static void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;
    int val;

    sprintf(FILENAME, "%s.%d", DEFAULT_FILENAME,getpid());

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
		
        /* -closeBetween   */
        if (strcmp(argv[idx], "-closeBetween") == 0) {
            idx++;
            closeBetween = 1;
            continue;
        }	
        /* -closeAfter   */
        if (strcmp(argv[idx], "-closeAfter") == 0) {
            idx++;
            closeAfter = 1;
            continue;
        }	
		
        /* -partial   */
        if (strcmp(argv[idx], "-partial") == 0) {
            idx++;
            ckeck_mode = CHECK_MODE_PARTIAL;
            continue;
        }

        /* -total   */
        if (strcmp(argv[idx], "-total") == 0) {
            idx++;
            ckeck_mode = CHECK_MODE_TOTAL;
            continue;
        }

        /* -random   */
        if (strcmp(argv[idx], "-random") == 0) {
            idx++;
            ckeck_mode = CHECK_MODE_RANDOM;
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
   
    memcpy(&pCompareBuff[offset],pBlock,blockSize);
//   printf("PWRITE off [%x,%x] size %d\n",offset,offset+blockSize,blockSize);
    size = pwrite(f, pBlock, blockSize, offset);
    if (size != blockSize) {
        printf("proc %3d - pwrite %d\n",myProcId,errno);
        printf("proc %3d - Can not write %d size at offset %d\n", myProcId, blockSize, offset);
        return -1;
    }
    return 0;

}
int do_total_read_and_check(int f) {
    ssize_t size;
    int idx2;
    size = pread(f, pReadBuff, READ_BUFFER_SIZE, 0);
    if (size <= 0) {
        printf("proc %3d - pread %d\n",myProcId,errno);
        printf("proc %3d - Can not read size %llu\n", myProcId, READ_BUFFER_SIZE);    
        return -1;
    }
//    printf("PREAD size = %d (%x)\n",size,size);
    for (idx2 = 0; idx2 < size; idx2++) {
        if (pReadBuff[idx2] != pCompareBuff[idx2]) {
            printf("\nproc %3d - offset %d = 0x%x contains %x instead of %x\n", myProcId, idx2, idx2, pReadBuff[idx2],pCompareBuff[idx2]);
	    printf("proc %3d - file (%d has been read)\n", myProcId,(int)size);
            hexdump(pReadBuff,idx2-16, 64);	    
	    printf("proc %3d - ref buf\n", myProcId);
            hexdump(pCompareBuff,idx2-16, 64); 
            return -1;
        }
    }
    
    return 0;
}
int do_random_read_and_check(int f) {
    ssize_t      size;
    int          idx2;    
    unsigned int nbControl;
    unsigned int blockSize;
    unsigned int offset;    

    nbControl = loop;    
    while (nbControl--) {

      offset    = (random()+offset)    % file_mb; 
      blockSize = (random()+blockSize) % RANDOM_BLOCK_SIZE;      
      if (blockSize == 0) blockSize = 1;

      memset(pReadBuff,0,blockSize);
      size = pread(f, pReadBuff, blockSize, offset);
      if (size < 0) {
        printf("proc %3d - pread %d\n",myProcId,errno);
        printf("proc %3d - Can not read offset %u size %u\n", myProcId, offset, blockSize);
        return -1;
      }      
      for (idx2 = 0; idx2 < blockSize; idx2++) {
          if (pReadBuff[idx2] != pCompareBuff[idx2+offset]) {
              printf("\nproc %3d - offset %d = 0x%x contains %x instead of %x\n", myProcId, idx2+offset, idx2+offset, 
		      pReadBuff[idx2],pCompareBuff[idx2+offset]);
	      printf("proc %3d - file (%d has been read)\n", myProcId,(int)size);
              hexdump(pReadBuff,idx2-16, 64);	    
	      printf("proc %3d - ref buf\n", myProcId);
              hexdump(pCompareBuff,idx2+offset-16, 64); 
              return -1;
          }
      }
    }
    return 0;
}
int do_partial_read_and_check(int f) {
    ssize_t size;
    int idx=0,idx2;
    int offsetStart;
    
    while (1) {
    
      /* Skip non written parts of the file */
      while ((idx<READ_BUFFER_SIZE) && (pCompareBuff[idx] == 0)) idx++;
      
      if (idx == READ_BUFFER_SIZE) return 0;
      
      /* How much bytes to compare */
      offsetStart = idx;
      while ((idx<READ_BUFFER_SIZE) && (pCompareBuff[idx] != 0)) idx++; 

      size = pread(f, pReadBuff, idx-offsetStart, offsetStart);
      if (size <= 0) {
        printf("proc %3d - pread %d\n",myProcId,errno);
        printf("proc %3d - Can not read size %u\n", myProcId, idx-offsetStart);    
        return -1;
      }      
      if (size != (idx-offsetStart)) {
        printf("proc %3d - pread %d\n",myProcId,errno);
        printf("proc %3d - Can only read size %lld\n", myProcId, (long long) size);    
        return -1;
      }            
      
      /* Compare */
      for (idx2 = 0; idx2 < size; idx2++) {
        if (pReadBuff[idx2] != pCompareBuff[idx2+offsetStart]) {
            printf("\nproc %3d - offset %d = 0x%x contains %x instead of %x\n", 
	             myProcId, idx2+offsetStart, idx2+offsetStart,
		     pReadBuff[idx2],pCompareBuff[idx2+offsetStart]);
	    printf("proc %3d - file (%d has been read)\n", myProcId,(int)size);
            hexdump(pReadBuff,idx2-16, 64);	    
	    printf("proc %3d - ref buf\n", myProcId);
            hexdump(pCompareBuff,idx2+offsetStart-16, 64); 
            return -1;
        }
      }
    }
 
    return 0;
}
int do_one_test(int * f, char * filename, int count) {
    unsigned int blockSize;
    unsigned int offset;
    unsigned int nbWrite;
    int          ret;


//    nbWrite = 1 + count % 3;
    nbWrite = 1 ;    
    while (nbWrite--) {

      offset    = (random()+offset)    % file_mb; 
      blockSize = (random()+blockSize) % RANDOM_BLOCK_SIZE;      
      if (blockSize == 0) blockSize = 1;

      if (do_write_offset(*f, offset, blockSize) != 0) {
	printf("proc %3d - ERROR !!! do_write_offset blocksize %6d  - offset %6d\n", myProcId, blockSize, offset);
	close(*f);      
	return -1;
      }  
    }
        
    if (closeBetween) {
      ret = close(*f);
      if (ret != 0) {
          printf("proc %3d - close %d\n",myProcId,errno);
          printf("proc %3d - Can not close %s\n", myProcId, filename);
          printf("proc %3d - last offset %d size %d block %d\n", myProcId, offset,blockSize, offset/8096);
          return -1;
      }

      *f = open(filename, O_RDWR);
      if (*f < 0) {
          printf("proc %3d - re-open %d\n",myProcId,errno);
          printf("proc %3d - Can not re-open %s\n", myProcId,filename);
          return -1;
      }
    }
    
    switch (ckeck_mode) {
      case CHECK_MODE_TOTAL:
	return do_total_read_and_check(*f);
	break;
	
      case CHECK_MODE_PARTIAL:	
	return do_partial_read_and_check(*f);
	break;
	
      case CHECK_MODE_RANDOM:	
	return do_random_read_and_check(*f);
	break;
	
      default:
        printf("proc %3d - unknown ckeck_mode %d\n",myProcId,ckeck_mode);   	
        return -1;    
    }
}
int read_empty_file(char * filename) {
    int f;
    ssize_t size;


    f = open(filename, O_RDWR | O_CREAT, 0640);
    if (f == -1) {
        printf("proc %3d - open %d\n",myProcId, errno);
        printf("proc %3d - Can not open %s\n",myProcId, filename);
        return 0;
    }


    size = pread(f, pReadBuff, READ_BUFFER_SIZE, 0);
    if (size < 0) {
        printf("proc %3d - pread %d\n",myProcId,errno);
        printf("proc %3d - Can not read %s size %llu\n", myProcId, filename, READ_BUFFER_SIZE);    
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

int loop_test_process() {
  int count=0; 
  char filename[500];
  char c;
  char * pChar;
  int    fd;
  
  pBlock = NULL;
  pBlock = malloc(RANDOM_BLOCK_SIZE + 10);
  if (pBlock == NULL) {
      printf("Can not allocate %d bytes\n", RANDOM_BLOCK_SIZE+1);
      perror("malloc");
      return -1;
  }  
  
  pChar = pBlock;
  c = 'A'+ (myProcId%26);
  while ((pChar - pBlock)<RANDOM_BLOCK_SIZE) {
    pChar += sprintf(pChar,"%c%2.2d/", c,myProcId);
    if  (c == 'Z') {
      c = 'a';
    }	
    else if (c == 'z') {
      c = 'A';
    }	
    else c++;    
  }
    
  // Prepare a working buffer to read from or write to the file
  pReadBuff = NULL;
  pReadBuff = malloc(READ_BUFFER_SIZE);
  if (pReadBuff == NULL) {
      printf("Can not allocate %llu bytes\n", READ_BUFFER_SIZE);
      perror("malloc");
      return -1;
  }
  memset(pReadBuff,0,READ_BUFFER_SIZE);

  // Buffer containing what the file should contain
  pCompareBuff = NULL;
  pCompareBuff = malloc(READ_BUFFER_SIZE);
  if (pCompareBuff == NULL) {
      printf("Can not allocate %llu bytes\n", READ_BUFFER_SIZE);
      perror("malloc");
      return -1;
  }
  memset(pCompareBuff,0,READ_BUFFER_SIZE);  
    
  sprintf(filename,"%s.%d",FILENAME, myProcId);
  if (unlink(filename) == -1) {
    if (errno != ENOENT) {
      printf("proc %3d - ERROR !!! can not remove %s %s\n", myProcId, filename, strerror(errno));
      return -1;
    }
  }

  fd = open(&filename[0], O_RDWR | O_CREAT, 0640);
  if (fd == -1) {
      printf("proc %3d - open %d\n",myProcId, errno);
      printf("proc %3d - Can not open %s\n",myProcId, filename);
      return -1;
  }
          
  while (1) {
    count++;    
    if  (do_one_test(&fd,filename,count) != 0) {
      printf("proc %3d - ERROR in loop %d\n", myProcId, count);   
      close(fd);   
      return -1;
    } 
    
    if (loop==count) {
      close(fd);
      return 0;
    }
      
    if (closeAfter) {
      close(fd);
      fd = open(&filename[0], O_RDWR);
      if (fd == -1) {
	  printf("proc %3d - open %d\n",myProcId, errno);
	  printf("proc %3d - Can not open %s\n",myProcId, filename);
	  return -1;
      }             
    }  
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
