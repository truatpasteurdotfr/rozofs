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

#define PATH "mnt1_1/rebuild/"

#define DEFAULT_NBFILE       30000

typedef enum _action_e {
  ACTION_NONE,
  ACTION_CREATE,
  ACTION_DELETE,  
  ACTION_CHECK,
  ACTION_CHECK_ONE
} action_e;
action_e action  = ACTION_NONE;
int      nbfiles = DEFAULT_NBFILE;

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


static void usage() {
    printf("Parameters:\n");
    printf("[ -nbfiles <NB> ]                               Number of files (default %s)\n", DEFAULT_NBFILE);
    printf("[ -action <create|check|checkone|delete> ]      What to do with these files\n");
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

	
	
        /* -action <create|check|delete>  */
        if (strcmp(argv[idx], "-action") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            if      (strcmp(argv[idx],"create")==0) action = ACTION_CREATE;
	    else if (strcmp(argv[idx],"delete")==0) action = ACTION_DELETE;
	    else if (strcmp(argv[idx],"check")==0)  action = ACTION_CHECK;
	    else if (strcmp(argv[idx],"checkone")==0)  action = ACTION_CHECK_ONE;
	    else {
              printf("%s option but bad value \"%s\"!!!\n", argv[idx-1], argv[idx]);
	      usage();	      
	    }
            idx++;
            continue;
        }
					
	/* -nbfiles <NB> */
        if (strcmp(argv[idx], "-nbfiles") == 0) {
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
	    nbfiles = val;
            idx++;
            continue;
        }	
			
        printf("Unexpected parameter %s\n", argv[idx]);
        usage();
    }
}
#define LOOP_NB  37
#define BLKSIZE (1024*8)
char    refblock[BLKSIZE];
char    readblock[BLKSIZE];

void update_block(int f, int b) {
  char string[64];
  int len;
  
  len = sprintf(string,"\n--%8.8d--%8.8d--\n",f,b);
  memcpy(refblock,string,len);
  
}
void init_block() {
  int  idx;
  char car;

  car = '0';
  for (idx=0; idx<BLKSIZE; idx++) {
    refblock[idx] = car;
    switch(car) {
      case '9': car = 'a'; break;
      case 'z': car = 'A'; break;
      case 'Z': car = '0'; break;  
      default:
      car++;
    }    
  }
}
char path_file_name[128];
char * getfilename(int idx) {
  sprintf(path_file_name,"%d", idx);
  return path_file_name;
}

int delete() {
  int idx;
  char * fname;

  for (idx=1; idx <= nbfiles; idx++) {
    fname = getfilename(idx); 
    unlink(fname);
  }        
}
void check_one(int idx) {
  int    blkidx;
  char * fname;
  int    fd=-1;
  int    ret;
  int    i;

  fname = getfilename(idx); 

  fd = open(fname, O_RDONLY);
  if (fd < 0) {
    printf("CHECK open(%s) %s\n", fname, strerror(errno));
    exit(-1);
  }

  for (blkidx=0; blkidx < LOOP_NB; blkidx++) {

    ret = pread(fd, readblock, BLKSIZE, blkidx*BLKSIZE);
    if (ret < 0) {
      printf("CHECK pread(%s) block %d %s\n", fname, blkidx, strerror(errno));
      exit(-1);
    }
    if (ret != BLKSIZE) {
      printf("CHECK pread(%s) block %d too short %d/%d\n", fname, blkidx, ret,BLKSIZE);
      exit(-1);      
    }

    update_block(idx,blkidx);

    for(i=0; i<BLKSIZE; i++) {
      if (readblock[i] != refblock[i]) {
        printf("CHECK %s bad content in block %d offset %d\n", fname, blkidx, i);
	if (i<21) i = 0;
	ret = 80;
	if ((ret+i) >= BLKSIZE) ret = BLKSIZE-i;
	hexdump(readblock, i, ret);
	printf("---ref\n");
	hexdump(refblock, i, ret);
        exit(-1);
      }	  
    } 
  } 

  ret = close(fd);
  if (ret < 0) { 	    
    printf("CHECK close(%s) %s\n", fname, strerror(errno));
    exit(-1);
  }        
}
void check() {
  int idx;

  for (idx=1; idx <= nbfiles; idx++) {
    check_one(idx);
  }        
}
int create() {
  int idx,loop;
  char * fname;
  int    fd=-1;
  int    ret;
  
  for (loop=0; loop < LOOP_NB; loop++) {
    for (idx=1; idx <= nbfiles; idx++) {

      fname = getfilename(idx); 
      update_block(idx,loop);

      if (loop == 0) fd = open(fname, O_CREAT | O_TRUNC | O_WRONLY, 0640);
      else           fd = open(fname, O_WRONLY);
      if (fd < 0) {
	printf("CREATE open(%s) loop %d %s\n", fname,loop, strerror(errno));
	exit(-1);
      }
      ret = pwrite(fd, refblock, BLKSIZE, loop*BLKSIZE);
      if (ret != BLKSIZE) {
	printf("CREATE write(%s) size %d offset %d %s\n", fname, BLKSIZE, loop, strerror(errno));
	exit(-1);
      }
      ret = close(fd);
      if (ret < 0) { 	    
	printf("CREATE close(%s) %s\n", fname, strerror(errno));
	exit(-1);
      }
    }
  }    
}


int main(int argc, char **argv) {
    
  read_parameters(argc, argv);

  if (action == ACTION_NONE) {
    usage();
  }
  
  init_block();


  if (mkdir(PATH, 0640) != 0) { 
    if (errno != EEXIST) {
      printf("mkdir(%s) %s",PATH,strerror(errno));
      exit(-1);
    }
  }
  
  if( chdir(PATH) != 0) {
    printf("chdir(%s) %s",PATH,strerror(errno));
    exit(-1);
  }
 
  switch(action) {

    case ACTION_CREATE:  
      create();
      break;

    case ACTION_DELETE: 
      delete();
      break;

    case ACTION_CHECK:
      check();
      break;
    case ACTION_CHECK_ONE:
      check_one(nbfiles);
      break;      
  }
   
  exit(0);
}
