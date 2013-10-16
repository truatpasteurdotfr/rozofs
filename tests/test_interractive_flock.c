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
#include <time.h>



#define DEFAULT_NB_PROCESS    20
#define DEFAULT_LOOP         200

#define READ_BUFFER_SIZE       (32*1024)


int shmid;
#define SHARE_MEM_NB 7539

#define FILE_SIZE 5000

char FILENAME[500];
char MYFILENAME[500];
char * pReadBuff    = NULL;
char * pCompareBuff = NULL;
int nbProcess       = DEFAULT_NB_PROCESS;
int myProcId;
int loop=DEFAULT_LOOP;
int * result;
int blocking=1;
int display=0;
int bsd=0;

static void usage() {
    printf("Parameters:\n");
    printf("-file <name>       file to do the test on\n" );
    printf("-nonBlocking       Lock in non blocking mode (default is blocking)\n");
    printf("-bsd               BSD mode lock (default is POSIX)\n");
    printf("-display           Display lock traces\n");
    printf("[ -process <nb> ]  The test will be done by <nb> process simultaneously (default %d)\n", DEFAULT_NB_PROCESS);
    printf("[ -loop <nb> ]     <nb> test operations will be done (default %d)\n",DEFAULT_LOOP);
    exit(-100);
}
void do_sleep_ms(int ms) {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = ms*1000000;   
    nanosleep(&req, NULL);
}    
void do_sleep_sec(int sec) {
    struct timespec req;
    req.tv_sec = sec;
    req.tv_nsec = 0;   
    nanosleep(&req, NULL);
}    

int create_file(char * name, int size)  {
  char c;
  int fd;
  
  unlink(name);
  
  fd = open(name, O_RDWR | O_CREAT, 0640);
  if (fd < 0) {
    printf("open(%s) %s\n",name,strerror(errno));
    return -1;
  }
  c = 0;
  pwrite(fd,&c, 1, size-1);  // Size the file
  return fd;
}
int display_flock(int ope, struct flock * flock,int retry) {
  char  msg[128];
  char *p = &msg[0];
  
  p += sprintf(p,"PROC %3d ", myProcId);
  switch(ope) {
    case F_GETLK: 
      p += sprintf(p,"GET      ");
      break;
    case F_SETLK: 
      p += sprintf(p,"SET      ");
      break; 
    case F_SETLKW: 
      p += sprintf(p,"SET WAIT ");
      break; 
    default:
      p += sprintf(p,"RESULT %3d    ",retry);
      break;
  }
                    
  switch(flock->l_type) {
    case F_RDLCK: p += sprintf(p,"READ   "); break;
    case F_WRLCK: p += sprintf(p,"WRITE  "); break; 
    case F_UNLCK: p += sprintf(p,"UNLOCK "); break; 
    default: p += sprintf(p,"type=%d ",flock->l_type);
  }
  
  switch(flock->l_whence) {
    case SEEK_SET: p += sprintf(p,"START"); break;
    case SEEK_CUR: p += sprintf(p,"CURRENT"); break; 
    case SEEK_END: p += sprintf(p,"END"); break; 
    default: p += sprintf(p,"whence=%d ",flock->l_whence);
  }
  p += sprintf(p,"%+d len %+d pid %p", flock->l_start,flock->l_len,flock->l_pid);
  printf("%s\n",msg);
}
#define RETRY 8000
int set_lock (int fd, int code , int ope, int start, int stop, int posRef) {
  int ret;
  int retry=RETRY;
  struct flock flock;
        
  flock.l_type   = ope;
  flock.l_whence = posRef;
  flock.l_pid    = getpid();
  switch(posRef) {

    case SEEK_END:
      flock.l_start  = start-FILE_SIZE;
      if (stop == 0) flock.l_len = 0;
      else           flock.l_len = stop-start;
      break;
      
    default:  
      flock.l_start  = start;
      if (stop == 0) flock.l_len = 0;
      else           flock.l_len = stop-start;
  }                
  if (display) display_flock(code,&flock,0);
  
  while (retry) {
    retry--;
    ret = fcntl(fd, code, &flock);
    if (ret == 0) break;
    if ((code == F_SETLKW)||(ope == F_UNLCK)||(errno != EAGAIN)) {
      printf("proc %3d - fnctl() errno %d %s\n", myProcId, errno, strerror(errno));
      return -1;
    }      
  }
  if (retry == 0) {
    printf("proc %3d - fnctl() max retry %d %s\n", myProcId, errno, strerror(errno));
    return -1;    
  }
  
  if (display) printf("PROC %3d %d retry\n", myProcId, RETRY-retry);

  return 0;
}
int set_lock_len (int fd,int ope, int start, int len) {
  int ret;
  int retry=RETRY;
  struct flock flock;
        
  flock.l_type   = ope;
  flock.l_whence = SEEK_SET;
  flock.l_pid    = getpid();
  flock.l_start  = start;
  flock.l_len    = len;

  ret = fcntl(fd, F_SETLK, &flock);
  if (ret<0) {
      printf("proc %3d - fnctl() errno %d %s\n", myProcId, errno, strerror(errno));
  }
  return ret;
}

typedef enum _flock_mode_e {
  FLOCK_MODE_READ,
  FLOCK_MODE_WRITE
} FLOCK_MODE_E;

#define INPUT_SIZE 1024
char input[INPUT_SIZE];
int main(int argc, char **argv) {
  int ret;
  int fd;
  int start,len;
    
  sprintf(FILENAME, "%s", "mnt1/lolock");
   
  fd = create_file(FILENAME,10000);
  if (fd < 0) exit(-1000);
  printf("<r|w|f|e> <start> <len> :");  

  while (fgets(input,INPUT_SIZE,stdin)) {
    if (input[0] == 'r') {
      sscanf(input,"r %d %d", &start,&len);
      printf("SET READ LOCK from %d len %d\n",start,len);      
      ret = set_lock_len(fd, F_RDLCK, start, len);  
      if (ret< 0) printf("failed\n");
    }
    else if (input[0] == 'w') {
      sscanf(input,"w %d %d", &start,&len);
      printf("SET WRITE LOCK from %d len %d\n",start,len);      
      ret = set_lock_len(fd , F_WRLCK, start, len);    
      if (ret< 0) printf("failed\n");
    }
    else if (input[0] == 'f') {
      sscanf(input,"f %d %d", &start, &len);
      printf("FREE LOCK from %d len %d\n",start,len);      
      ret = set_lock_len(fd , F_UNLCK, start, len);        
      if (ret< 0) printf("failed\n");
    } 
    else if (input[0] == 'e') {
       break;
    }        
    sprintf(input,"attr -g rozofs %s", FILENAME);
    system(input);
    printf("<r|w|f|e> <start> <len> :");  
  }  
  close(fd);
  exit(ret);
}
