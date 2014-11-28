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
#include <pthread.h> 

#define DEFAULT_FILENAME    "mnt1_1/this_is_the_default_rw_2_proc_test_file_name"

#define BUFFER_SIZE   (256*1024)


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
uint32_t loopCount;


static void usage() {
    printf("Parameters:\n");
    printf("[ -file <name> ]           file to do the test on (default %s)\n", DEFAULT_FILENAME);
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
        /* -loop <loop count> */
        if (strcmp(argv[idx], "-loop") == 0) {
            idx++;
            if (idx == argc) {
                printf("%s option set but missing value !!!\n", argv[idx-1]);
                usage();
            }
            ret = sscanf(argv[idx], "%d", &loopCount);
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
void do_sleep_ms(int ms) {
    struct timespec req;
    req.tv_sec = 0;
    req.tv_nsec = ms*1000000;   
    nanosleep(&req, NULL);
}  
typedef enum _test_who_e {
  TEST_READER,
  TEST_WRITER,
  TEST_MASTER
} test_who_e;
typedef struct _test_ctx_t {
  char       filename[256];
  int        loop;
  char       block[BUFFER_SIZE];
  uint64_t   offset;
  uint64_t   size;
  int        reader_res;
  int        writer_res;  
  int        who;
} test_ctx_t;



void * loop_writer_thread(void * ctx) {
  int      count=0; 
  int      fd;
  uint64_t size;
  test_ctx_t * pCtx = (test_ctx_t *) ctx;

  

  fd = open(pCtx->filename, O_RDWR);
  if (fd == -1) {
      pCtx->writer_res = -1;
      printf("WRITER - ERROR !!! open %d\n",errno);
      printf("WRITER - ERROR !!! Can not open %s\n",pCtx->filename);
      pCtx->who        = TEST_MASTER;
      return NULL;
  }
          
  while (1) {
    count++;    


    /* Choose random offset and size to write */
//    pCtx->offset  = random() % (BUFFER_SIZE-1);
    pCtx->offset  = 0;
//    pCtx->size    = random() % (BUFFER_SIZE-pCtx->offset);
    pCtx->size    = 132;    
    if (pCtx->size == 0) pCtx->size = 1;

    //printf("WRITER loop %d offset %d size %d\n",count,pCtx->offset,pCtx->size);
        
    size = pwrite(fd, pCtx->block, pCtx->size, pCtx->offset);    
    if (size != pCtx->size) {
        pCtx->writer_res = -1;
        printf("WRITER - ERROR !!! pwrite %d\n",errno);
        printf("WRITER - ERROR !!! Can not write %d size at offset %d\n", pCtx->size, pCtx->offset);
        pCtx->who        = TEST_MASTER;
        return NULL;
    }

    pCtx->who = TEST_READER;

    if (pCtx->loop==count) {
      close(fd);
      return NULL;
    }
        
    
    while (pCtx->who == TEST_READER) {
      do_sleep_ms(1);
    }
  }  
}  
char *  readerBlock[BUFFER_SIZE]; 
void * loop_reader_thread(void * ctx) {
  int      count=0; 
  int      fd;
  int      i;
  uint64_t size;
  test_ctx_t * pCtx = (test_ctx_t *) ctx;

  fd = open(pCtx->filename, O_RDWR);
  if (fd == -1) {
      pCtx->reader_res = -1;
      printf("READER - ERROR !!! open %d\n",errno);
      printf("READER - ERROR !!! Can not open %s\n",pCtx->filename);
      pCtx->who        = TEST_MASTER;
      return NULL;
  }
          
  while (1) {

    if (pCtx->loop==count) {
      pCtx->reader_res = 0;
      close(fd);    
      pCtx->who = TEST_MASTER;
      return NULL;
    }    
    count++;    


    while (pCtx->who != TEST_READER) {
      do_sleep_ms(1);
    }
    
    //printf("READER loop %d offset %d size %d\n",count,pCtx->offset,pCtx->size);
    
    size = pread(fd, readerBlock, pCtx->size, pCtx->offset);    
    if (size != pCtx->size) {
        pCtx->reader_res = -1;
        printf("READER - ERROR !!! pread %d\n",errno);
        printf("READER - ERROR !!! Can not read %d size at offset %d\n", pCtx->size, pCtx->offset);
        pCtx->who        = TEST_MASTER;
        return NULL;
    }

    if (memcmp(readerBlock,pCtx->block,pCtx->size) != 0) {
      pCtx->reader_res = -1;
      printf("The buffers differ\n");
      pCtx->who        = TEST_MASTER;
      return NULL;
    }
        
    pCtx->who = TEST_WRITER;
  }  
}  
int start_thread_create(test_ctx_t * ctx, test_who_e who) {
   int                        i;
   int                        err;
   pthread_attr_t             attr;
   pthread_t                  thread;
   
   err = pthread_attr_init(&attr);
   if (err != 0) {
     printf("pthread_attr_init %d %s",who,strerror(errno));
     return -1;
   }  
   if (who == TEST_READER)
     err = pthread_create(&thread,&attr,loop_reader_thread,ctx);
   else
     err = pthread_create(&thread,&attr,loop_writer_thread,ctx);       
   if (err != 0) {
     printf("pthread_create %d %s",who,strerror(errno));
     return -1;
   }  
  return 0;
}

test_ctx_t ctx;
int loop_test_process() {
  int        i,ret;  
  int        size;
  int        fd;

  for (i=0; i< BUFFER_SIZE; i++) ctx.block[i] = i;
  
  strcpy(ctx.filename,FILENAME);
  if (unlink(ctx.filename) == -1) {
    if (errno != ENOENT) {
      printf("can not remove %s %s\n", ctx.filename, strerror(errno));
      return -1;
    }
  }

  fd = open(ctx.filename, O_RDWR | O_CREAT, 0640);
  if (fd == -1) {
      printf("open %d\n",errno);
      printf("Can not open %s\n",ctx.filename);
      return -1;
  }
  
  size = pwrite(fd, ctx.block, BUFFER_SIZE, 0);    
  if (size != BUFFER_SIZE) {
        printf("pwrite %d\n",errno);
        printf("Can not write %d size at offset %d\n", BUFFER_SIZE, 0);
        return -1;
  }
  close(fd);
 
  ctx.who = TEST_WRITER; 
  ctx.reader_res = 0;
  ctx.writer_res = 0;
  ctx.loop       = loopCount;
   
  /* Launch writer /reader threads */
  ret = start_thread_create(&ctx,TEST_WRITER);
  if (ret != 0) return ret;

  ret = start_thread_create(&ctx,TEST_READER);
  if (ret != 0) return ret;

  while (ctx.who != TEST_MASTER) {
      do_sleep_ms(1000);
  }  
  if ((ctx.reader_res == 0) && (ctx.writer_res == 0)) return 0;
  return -1;

}  

int main(int argc, char **argv) {
  pid_t pid[2000];
  int proc;
  int ret;
    
  read_parameters(argc, argv);
  ret = loop_test_process();
  return ret;

}
