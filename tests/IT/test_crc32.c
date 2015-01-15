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


#define DEFAULT_MNT         "mnt1_1"
#define DEFAULT_FILENAME    "this_is_the_default_crc32_test_file_name"
#define DEFAULT_NB_PROCESS    20
#define DEFAULT_LOOP         200
#define DEFAULT_FILE_SIZE_MB   1

#define RANDOM_BLOCK_SIZE      (9*1024)
#define READ_BUFFER_SIZE       (RANDOM_BLOCK_SIZE+file_mb)

#define ERROR(...) printf("%d proc %d %s - ", __LINE__,myProcId,filename); printf(__VA_ARGS__)


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

int nbProcess       = DEFAULT_NB_PROCESS;
int myProcId;
  

long long unsigned int file_mb=DEFAULT_FILE_SIZE_MB*1000000;

int * result;

static void usage() {
    printf("Parameters:\n");
    printf("[ -mount <mount> ]         The mount point (default %s)\n", DEFAULT_MNT);
    printf("[ -file <name> ]           file to do the test on (default %s)\n", DEFAULT_FILENAME);
    printf("[ -process <nb> ]          The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    exit(-100);
}

static void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;
    int val;

    char * mnt = NULL;
    char * fname = NULL;
    

    idx = 1;
    while (idx < argc) {

        /* -file <name> */
        if (strcmp(argv[idx], "-file") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            fname = argv[idx];
            idx++;
            continue;
        }
        /* -mnt <name> */
        if (strcmp(argv[idx], "-mount") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            mnt = argv[idx];
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
    
    
    char * p = FILENAME;
    if (mnt == NULL) {
      p += sprintf(p,"%s/",DEFAULT_MNT);
    }
    else {
      p += sprintf(p,"%s/",mnt);
    }
    if (fname == NULL) {
      p += sprintf(p,"%s",DEFAULT_FILENAME);
    }
    else {
      p += sprintf(p,"%s",fname);
    }    
    p += sprintf(p,".%d",getpid());    
}


int get_1rst_projection_file_name(char * fname,int len) {
  char   cmd[1024];
  int    fd;
  size_t size;
    
  sprintf(cmd,"./setup.sh cou %s | grep \"bins\" | awk \'NR==1{print $2}\' > /tmp/%d", fname, getpid()); 
//  printf("%s\n",cmd);
  system(cmd);

  sprintf(cmd,"/tmp/%d",getpid());
  fd = open(cmd, O_RDONLY, 0640);
  if (fd == -1) {
      printf("proc %3d - open(%s) %s\n",myProcId, cmd, strerror(errno));
      return -1;
  }          
  size = pread(fd, fname, len,0);
  if (size <= 0) {
      printf("proc %3d - pread(%s) -> %d %s\n", myProcId, cmd, size, strerror(errno));   
      return -1;
  }  
  fname[size-1] = 0;
  close(fd);  
  unlink(cmd);
  
  return 0;  
}
int create_projection_error(char * filename) {
  int      fd;
  uint32_t val32;
  size_t   size;
  char fname[500];
  int  valint;
  
  strcpy(fname,filename);
  
  if (get_1rst_projection_file_name(fname,500) != 0) return -1;
  
  fd = open(fname, O_RDWR, 0640);
  if (fd == -1) {
      printf("proc %3d - open(%s) %s\n",myProcId, fname, strerror(errno));
      return -1;
  }
  
  size = pread(fd, &val32, sizeof(val32),16);
  if (size != sizeof(val32)) {
      printf("proc %3d - pread(%s) -> %d %s\n", myProcId, fname, size, strerror(errno));   
      return -1;
  }    
  
  val32++;
  
  size = pwrite(fd, &val32, sizeof(val32),16);
  if (size != sizeof(val32)) {
      printf("proc %3d - pwrite(%s) -> %d %s\n", myProcId, fname, size, strerror(errno));   
      return -1;
  }   
  
  close(fd); 
  sync();
  return 0;
}
int loop_test_process() {
  char filename[500];
  int    fd;
  size_t size;
  int valint;
  
  /*
  ** Create and write a file
  */
  sprintf(filename,"%s.%d",FILENAME, myProcId);
  if (unlink(filename) == -1) {
    if (errno != ENOENT) {
      printf("proc %3d - ERROR !!! unlink(%s) %s\n", myProcId, filename, strerror(errno));
      return -1;
    }
  }

  fd = open(filename, O_RDWR | O_CREAT, 0640);
  if (fd == -1) {
      printf("proc %3d - open1(%s) %s\n",myProcId, filename, strerror(errno));
      return -1;
  }             
  size = pwrite(fd, &myProcId, sizeof(myProcId), 0);
  if (size != sizeof(myProcId)) {
      printf("proc %3d - pwrite(%s) -> %d %s\n", myProcId, filename, size, strerror(errno));   
      return -1;
  }  
  close(fd);
  sync();
  
  /*
  ** Wait for rozofs to write the projection files
  */
  sleep(1);
  
  /*
  ** Modify the content of the 1rst projection file
  */
  if (create_projection_error(filename) != 0) {
    return -1;
  }
  
  /*
  ** Read the file
  */
  fd = open(filename, O_RDWR | O_CREAT, 0640);
  if (fd == -1) {
      printf("proc %3d - open2(%s) %s\n",myProcId, filename, strerror(errno));
      return -1;
  }
  size = pread(fd, &valint, sizeof(valint), 0);
  if (size != sizeof(valint)) {
      printf("proc %3d - pread(%s) -> %d %s\n", myProcId, filename, size, strerror(errno));   
      return -1;
  }  
  close(fd);
  
  if (valint !=  myProcId) {
      printf("proc %3d - myProcId %d != valint %d\n", myProcId, myProcId, valint); 
      return -1;      
  }
  return 0;
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
      ret++;
    }
  }
  free_result();
  if (ret != 0) printf("OK %d / FAILURE %d\n",nbProcess-ret, ret);
  exit(ret);
}
