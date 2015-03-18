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

typedef enum _action_e {
  ACTION_NONE,
  ACTION_CREATE,
  ACTION_CORRUPT,  
  ACTION_REREAD
} action_e;
action_e action  = ACTION_NONE;

char * mnt   = NULL;
char * fname = NULL;
 
#define DEFAULT_FILENAME    "this_is_the_default_crc32_test_file_name"
#define DEFAULT_NB_PROCESS    20
#define DEFAULT_LOOP         200
#define DEFAULT_FILE_SIZE_MB   1
#define DEFAULT_KILO_SZ        1024

/*
** This has rather to be a string with a length divisor of 1024
*/
char   * attention = ">> This is the CRC32 test line.\n";

#define ERROR(...) printf("%d proc %d - ", __LINE__,myProcId); printf(__VA_ARGS__)

#define BLK_SIZE 1024

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



int nbProcess       = DEFAULT_NB_PROCESS;
int myProcId;
int nbKilo          = DEFAULT_KILO_SZ;


long long unsigned int file_mb=DEFAULT_FILE_SIZE_MB*1000000;

int * result;

static void usage(char * msg) {

    if (msg) printf("%s !!!\n",msg);

    printf("Parameters:\n");
    printf("[ -mount <mount> ]         The mount point\n");
    printf("[ -file <name> ]           file to do the test on\n");
    printf("[ -action <create|corrupt|reread> ]      What to do with these files\n");
    exit(-100);
}

static void read_parameters(argc, argv)
int argc;
char *argv[];
{
    unsigned int idx;
    int ret;
    int val;


    

    idx = 1;
    while (idx < argc) {

        /* -file <name> */
        if (strcmp(argv[idx], "-file") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage(NULL);
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
                usage(NULL);
            }
            mnt = argv[idx];
            idx++;
            continue;
        }	
	
        /* -action <create|check|delete>  */
        if (strcmp(argv[idx], "-action") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage(NULL);
            }
            if      (strcasecmp(argv[idx],"create")==0) action = ACTION_CREATE;
	    else if (strcasecmp(argv[idx],"corrupt")==0) action = ACTION_CORRUPT;
	    else if (strcasecmp(argv[idx],"reread")==0)  action = ACTION_REREAD;
	    else {
              printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
	      usage(NULL);	      
	    }
            idx++;
            continue;
        }
	
        /* -sz <kilo>  */
        if (strcmp(argv[idx], "-sz") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage(NULL);
            }
            ret = sscanf(argv[idx], "%u", &nbKilo);
            if (ret != 1) {
                printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
                usage(NULL);
            }
            idx++;
            continue;
        }			
        printf("Unexpected parameter %s\n", argv[idx]);
        usage(NULL);
    }
    
   if (fname==NULL) usage("Missing file name");
   if (mnt==NULL) usage("Miising mount point");   
   if (action == ACTION_NONE) usage ("Missing action");
}


int get_projection_nb_file_name(char * fname,int len, int nb) {
  char   cmd[BLK_SIZE];
  int    fd;
  size_t size;
    
  sprintf(cmd,"./tst.py cou %s | grep \"bins\" | awk \'NR==%d{print $2}\' > /tmp/%d; sync", fname, nb, getpid()); 
  //printf("%s\n",cmd);
  system(cmd);
  sprintf(cmd,"/tmp/%d",getpid());
  
//  usleep(100000);
  
  fd = open(cmd, O_RDONLY, 0640);
  if (fd == -1) {
      ERROR("open(%s) %s\n",cmd, strerror(errno));
      return -1;
  }          
  size = pread(fd, fname, len,0);
  if (size <= 0) {
      ERROR("pread(%s) -> %d %s\n", cmd, size, strerror(errno));   
      return -1;
  }  
  fname[size-1] = 0;
  close(fd);  
  unlink(cmd);
  
  return 0;  
}
int corrupt(char * filename) {
  int      fd;
  uint32_t val32;
  size_t   size;
  char fname[500];
  int      i;
  uint64_t offset;

  /*
  ** Find out a projection file name
  */
  strcpy(fname,filename);
  if (get_projection_nb_file_name(fname,500, myProcId%3+1) != 0) return -1;
  
  /*
  ** Open it
  */
  fd = open(fname, O_RDWR, 0640);
  if (fd == -1) {
      ERROR("open(%s) %s\n", fname, strerror(errno));
      return -1;
  }
  
  /*
  ** Read and rewite a 32 bit value
  */
  offset = 52;
  for (i=0; i< (nbKilo/5); i++, offset += (5*BLK_SIZE)) {
      
    size = pread(fd, &val32, sizeof(val32),offset);
    if (size == 0) break;
    if (size != sizeof(val32)) {
      ERROR("pread(%s,offset %d) -> %d %s\n", fname, offset, size, strerror(errno));   
      return -1;
    }    
  
    val32++;
  
    size = pwrite(fd, &val32, sizeof(val32),offset);
    if (size != sizeof(val32)) {
      ERROR("pwrite(%s,offset %d) -> %d %s\n", fname, offset, size, strerror(errno));   
      return -1;
    }   
  }  
  
  close(fd); 
  sync();
  return 0;
}
int reread(char * filename) {
  int    fd;
  size_t size;
  int    i,j;
  char   block[BLK_SIZE];
  uint64_t offset;
    
  /*
  ** Open file
  */
  fd = open(filename, O_RDWR | O_CREAT, 0640);
  if (fd == -1) {
      ERROR("open2(%s) %s\n", filename, strerror(errno));
      return -1;
  }

  /*
  ** Read the file
  */  
  offset = 0;
  for (i=0; i<nbKilo; i++,offset+=BLK_SIZE) {

    size = pread(fd, &block, BLK_SIZE, offset);
    if (size != sizeof(block)) {
	ERROR("pread(%s,offset %d) -> %d %s\n", filename, offset, size, strerror(errno));   
	return -1;
    }  
    
    for (j=0; j < BLK_SIZE; j++) {
      if (block[j] != attention[j%strlen(attention)]) {
        ERROR("Bad text in %s at offset %d\n", filename, offset+j); 
	return -1;       
      }
    }    
  }
  
  close(fd);
  return 0;
}
int create(char * filename) {
  int    fd;
  size_t size; 
  char   block[BLK_SIZE];
  int    i;
  uint64_t offset;

  /*
  ** Remove the file
  */
  if (unlink(filename) == -1) {
    if (errno != ENOENT) {
      ERROR("unlink(%s) %s\n", filename, strerror(errno));
      return -1;
    }
  }

  /*
  ** Create file empty
  */
  fd = open(filename, O_RDWR | O_CREAT, 0640);
  if (fd == -1) {
      ERROR("open1(%s) %s\n", filename, strerror(errno));
      return -1;
  }
  
  /*
  ** Prepare the 1K block
  */
  for (i=0; i < BLK_SIZE; i++) {
    block[i] = attention[i%strlen(attention)];
  }
    
  /*
  ** Write the file
  */         
  offset = 0;
  for (i=0; i<nbKilo; i++,offset+=BLK_SIZE) {
          
    size = pwrite(fd, block, BLK_SIZE, offset);
    if (size != sizeof(block)) {
      ERROR("pwrite(%s,offset %d) -> %d %s\n", filename, offset, size, strerror(errno));   
      close(fd);
      return -1;
    }

  }
      
  /*
  ** Close the file
  */    
  close(fd);
  sync();
}  

int main(int argc, char **argv) {
  char filename[256];
  char cwd[64];
  
  getcwd(cwd,64);
    
  read_parameters(argc, argv);
  sprintf(filename,"%s/%s/%s", cwd, mnt, fname);
   
  switch(action) {

    case ACTION_CREATE:  
      create(filename);
      break;

    case ACTION_CORRUPT: 
      corrupt(filename);
      break;

    case ACTION_REREAD:
      return reread(filename);
      break;
  }
   
  return (0);
}
