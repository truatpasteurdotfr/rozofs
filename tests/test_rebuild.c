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
  ACTION_CHECK
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
    printf("[ -nbfiles <NB> ]                      Number of files (default %s)\n", DEFAULT_NBFILE);
    printf("[ -action <create|check|delete> ]      What to do with these files\n");
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


int main(int argc, char **argv) {
  int ret;
  int fd;
  char path[128];
  char string[128];
  int size;
  int idx;
  
    
  read_parameters(argc, argv);

  if (action == ACTION_NONE) {
    usage();
  }

  mkdir(PATH, 0640);
  chdir(PATH);
  
  for (idx=1; idx <= nbfiles; idx++) {

    sprintf(path,"%d", idx);
    size = strlen(path)+1;
    
    switch(action) {
    
      case ACTION_CREATE:        
        fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0640);
	if (fd < 0) {
	  printf("open(%s) %s", path, strerror(errno));
	  exit(-1);
	}
        ret = write(fd, path, size);
	if (ret != size) {
	  printf("write(%s) size %d %s\n", path, size, strerror(errno));
	  exit(-1);
	}
	close(fd);	
        break;
	
      case ACTION_DELETE: 
        unlink(path);
        break;
	
      case ACTION_CHECK:
        fd = open(path, O_RDONLY);
	if (fd < 0) {
	  printf("open(%s) %s", path, strerror(errno));
	  exit(-1);
	}
        ret = read(fd, string, sizeof(string));
	if (ret < 0) {
	  printf("read(%s) %s", path, strerror(errno));
	  exit(-1);
	}
	close(fd);
	if (strcmp(string,path)!=0) {
	  printf("read(%s) bad content", path);
	  hexdump(string, 0, size);
	  exit(-1);  
	} 
        break;
    }
  }
  
  exit(0);
}
