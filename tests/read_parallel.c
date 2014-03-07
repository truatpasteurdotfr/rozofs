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
#include <sys/wait.h>




#define DEFAULT_NB_PROCESS    20
#define DEFAULT_LOOP         200

#define READ_BUFFER_SIZE       (32*1024)


int shmid;
#define SHARE_MEM_NB 7539


char FILENAME[500];

char * pReadBuff    = NULL;
char * pCompareBuff = NULL;
int nbProcess       = DEFAULT_NB_PROCESS;
int myProcId;
int loop=DEFAULT_LOOP;
int * result;

static void usage() {
    printf("Parameters:\n");
    printf("-file <name>       file to do the test on\n" );
    printf("[ -process <nb> ]  The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    printf("[ -loop <nb> ]     <nb> test operations will be done (default %d)\n",DEFAULT_LOOP);
    exit(-100);
}

static void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;

    FILENAME[0] = 0; 

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
			
        printf("Unexpected parameter %s\n", argv[idx]);
        usage();
    }
}

int do_one_test(char * filename, int count) {
  int f;
  unsigned int offset=0;
  ssize_t size;


  f = open(filename, O_RDWR | O_CREAT, 0640);
  if (f == -1) {
      printf("proc %3d - open %d\n",myProcId, errno);
      printf("proc %3d - Can not open %s\n",myProcId, filename);
      return -1;
  }

  // read 1rts 32K 
  size = pread(f, pCompareBuff, READ_BUFFER_SIZE, 0);
  if (size < READ_BUFFER_SIZE) {
      printf("proc %3d - pread %d\n",myProcId,errno);
      printf("proc %3d - Can not read size 32K %d\n", myProcId, (int)size);    
      close(f);
      return -1;
  }    

  while (1) {

    offset += size;
    size = pread(f, pReadBuff, READ_BUFFER_SIZE, offset);
    if (size < 0) {
      printf("proc %3d - pread %d\n",myProcId,errno);
      close(f);
      return -1;
    }

    if (size == 0) {
      close(f);      
      return 0;
    }

    if (memcmp(pCompareBuff,pReadBuff, size) != 0) {
      printf("Unexpected pattern in file\n");
      close(f);
      return -1;
    }
  }      
}
int loop_test_process() {
  int count=0;   
    
  // Prepare a working buffer to read from or write to the file
  pReadBuff = NULL;
  pReadBuff = malloc(READ_BUFFER_SIZE);
  if (pReadBuff == NULL) {
      printf("Can not allocate %d bytes\n", READ_BUFFER_SIZE);
      perror("malloc");
      return -1;
  }
  memset(pReadBuff,0,READ_BUFFER_SIZE);

  // Buffer containing what the file should contain
  pCompareBuff = NULL;
  pCompareBuff = malloc(READ_BUFFER_SIZE);
  if (pCompareBuff == NULL) {
      printf("Can not allocate %d bytes\n", READ_BUFFER_SIZE);
      perror("malloc");
      return -1;
  }
  memset(pCompareBuff,0,READ_BUFFER_SIZE);  
          
  while (1) {
    count++;    
    if  (do_one_test(FILENAME,count) != 0) {
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
  if (FILENAME[0] == 0) {
    printf("Missing mandatory parameter -file\n");
    exit(-100);
  }
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
  if (ret != 0) printf("OK %d / FAILURE %d\n",nbProcess+ret, -ret);
  exit(ret);
}
