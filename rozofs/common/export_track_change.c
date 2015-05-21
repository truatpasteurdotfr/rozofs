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

#define _XOPEN_SOURCE 500

#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <pthread.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/core/uma_dbg_api.h>
#include <sys/file.h>
#include "export_track_change.h"

int expt_thread_period_count;
uint64_t expt_poll_stats[2];
 uint64_t expt_write_bytes_count = 0;
 uint64_t expt_write_th_rq_count = 0;
 uint64_t expt_write_count = 0;
 uint64_t expt_flush_error = 0;
 uint64_t expt_write_error = 0;

#define EXPT_MAX_TRACK_CTX 32
int expt_thread_init_done = 0;
expt_ctx_t *expt_thread_ctx_p[EXPT_MAX_TRACK_CTX];
int expt_thread_max_context;       /**< max number of context supported by the thread */
int expt_thread_cur_context;       /**< current number of contexts                    */
static pthread_t exptctx_thread;
int expt_enable = 1;



int expt_thread_init();


#define START_PROFILING_TH(the_probe)\
    uint64_t tic, toc;\
    struct timeval tv;\
    {\
        the_probe[P_COUNT]++;\
        gettimeofday(&tv,(struct timezone *)0);\
        tic = MICROLONG(tv);\
    }

#define STOP_PROFILING_TH(the_probe)\
    {\
        gettimeofday(&tv,(struct timezone *)0);\
        toc = MICROLONG(tv);\
        the_probe[P_ELAPSE] += (toc - tic);\
    }

static char * show_expt_thread_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"expt_thread reset                : reset statistics\n");
  pChar += sprintf(pChar,"expt_thread disable              : disable export tracking thread\n");
  pChar += sprintf(pChar,"expt_thread enable               : enable export tracking thread\n");
  pChar += sprintf(pChar,"expt_thread period [ <period> ]  : change thread period(unit is second)\n");  
  return pChar; 
}


/*
**_______________________________________________________________
*/
char * show_expt_thread_stats_display(char *pChar)
{
     /*
     ** display the statistics of the thread
     */
     pChar += sprintf(pChar,"period     : %d second(s) \n",expt_thread_period_count);
     pChar += sprintf(pChar,"statistics :\n");
     pChar += sprintf(pChar," - buffer size       :%u\n",(unsigned int)EXPT_MAX_BUFFER_SZ);
     pChar += sprintf(pChar," - number of buffers :%u\n",(unsigned int)EXPT_MAX_BUFFER);
     pChar += sprintf(pChar," - update requests   :%llu\n",(long long unsigned int)expt_write_count);
     pChar += sprintf(pChar," - update errors     :%llu\n",(long long unsigned int)expt_write_error);
     pChar += sprintf(pChar," - flush errors      :%llu\n",(long long unsigned int)expt_flush_error);
     pChar += sprintf(pChar," - activation counter:%llu\n",
              (long long unsigned int)expt_poll_stats[P_COUNT]);
     pChar += sprintf(pChar," - average time (us) :%llu\n",
                      (long long unsigned int)(expt_poll_stats[P_COUNT]?
		      expt_poll_stats[P_ELAPSE]/expt_poll_stats[P_COUNT]:0));
     pChar += sprintf(pChar," - total time (us)   :%llu\n",(long long unsigned int)expt_poll_stats[P_ELAPSE]);
     pChar += sprintf(pChar," - total Write       : memory %llu MBytes (%llu Bytes) requests %llu\n\n",
             (long long unsigned int) expt_write_bytes_count / (1024*1024),
             (long long unsigned int) expt_write_bytes_count,
             (long long unsigned int) expt_write_th_rq_count);
     return pChar;
}

/*
**_______________________________________________________________
*/
void show_expt_thread(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();
    int ret;
    int period;
    
    
    if (argv[1] == NULL) {
      show_expt_thread_stats_display(pChar);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer()); 
      return;  	  
    }

    if (strcmp(argv[1],"reset")==0) {
      pChar = show_expt_thread_stats_display(pChar);
      pChar +=sprintf(pChar,"\nStatistics have been cleared\n");
      expt_poll_stats[0] = 0;
      expt_poll_stats[1] = 0;
      expt_write_count = 0;
      expt_write_error = 0;
      expt_flush_error = 0;
      expt_write_bytes_count = 0;
      expt_write_th_rq_count = 0;
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());  
      return;   
    }
    if (strcmp(argv[1],"period")==0) {   
	if (argv[2] == NULL) {
	show_expt_thread_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
	return;  	  
      }
      ret = sscanf(argv[2], "%d", &period);
      if (ret != 1) {
	show_expt_thread_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
	return;   
      }
      /*
      ** change the period of the thread
      */
      if (period == 0)
      {
        uma_dbg_send(tcpRef, bufRef, TRUE, "value not supported\n");
        return;
      }
      
      expt_thread_period_count = period;
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }
    if (strcmp(argv[1],"disable")==0) {   
	expt_enable = 0;
	uma_dbg_send(tcpRef, bufRef, TRUE,"inode tracking is disabled");   
	return;   
    }
    if (strcmp(argv[1],"enable")==0) {   
	expt_enable = 1;
	uma_dbg_send(tcpRef, bufRef, TRUE,"inode tracking is enabled");   
	return;   
    }
    show_expt_thread_help(pChar);	
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());    
    return;
}
/*
**__________________________________________________________________
*/
/**
*  allocation a context for tracking inode change or update

   @param name: name to the tracking inode context
   @retval <>NULL pointer to the allocated context
   @retval NULL: out of memory
*/
expt_ctx_t *expt_alloc_context(char *name)
{
    expt_ctx_t *p;
    int len;
    
    len=strlen(name);
    if (len >= EXPT_MAX_NAME) 
    {
       errno = EINVAL;
       return NULL;
    }
    /*
    ** allocate an entry in the pool of context of the inode tracking thread
    */
    if (expt_thread_init_done == 0)
    {
      if (expt_thread_init() < 0) return NULL;
    }
    if (expt_thread_cur_context >= expt_thread_max_context)
    {
      /*
      ** out of context
      */
      return NULL;
    }

    p= malloc(sizeof(expt_ctx_t));
    if (p == NULL) return NULL;
    memset(p,0,sizeof(expt_ctx_t));
    p->last_time_change = time(NULL);
    strcpy(p->name,name);
    /*
    ** allocate a lock for the context to exclude write and disk flush 
    ** processes
    */
    if (pthread_rwlock_init(&p->lock, NULL) != 0) {
        free(p);
        return NULL;
    } 
    /*
    ** put the context in the table pointer of the tracking thread
    */
    expt_thread_ctx_p[expt_thread_cur_context] = p;
    expt_thread_cur_context++;
    
    return p;
}
/*
**__________________________________________________________________
*/
/**
*  release a context useed for tracking inode change or update

   @param none
   @retval <>NULL pointer to the allocated context
   @retval NULL: out of memory
*/
void expt_release_context(expt_ctx_t *p)
{
    int i;
    int k;
    expt_ctx_buf_t *buf_p;
        
    if (p == NULL) return ;
    for (k= 0; k < 2; k++)
    {
      buf_p = &p->buf[k];
      for (i = 0; i < EXPT_MAX_BUFFER; i++)
      {
	 if (buf_p->bitmap_buf[i] !=NULL) free(buf_p->bitmap_buf[i]);
      }
    }
    free(p);
}
/*
**__________________________________________________________________
*/
/**
* assert the bit corresponding to inode on with there is a change
  This is done for the file that contains the inode
  
  @param slice: slice of the inode
  @param file_id: file id that contains the inode
  
  @retval 0 on success
  @retval -1 on error
*/
int expt_set_bit(expt_ctx_t *p,int slice,uint64_t file_id)
{
   int file_idx;
   int bit_id;
   int byte_offset;
   int buf_idx;
   int idx_in_buf;
   char *bitmap = NULL;
   expt_ctx_buf_t *buf_p;

   if (p== NULL) return -1;

   if ((errno = pthread_rwlock_wrlock(&p->lock)) != 0) {
       severe("can't lock inode tracking entry %s: %s",p->name, strerror(errno));
       expt_write_error++;
       return -1;
   }
   buf_p = &p->buf[p->cur_buf&0x1];  
   /*
   ** get the index of the buffer that is concerned by the inode change
   */
   file_idx = file_id%EXPT_BITMAP_SZ_BYTE;
   /*
   ** get the reel index by appending the slice
   */
   bit_id = file_idx+slice*(EXPT_BITMAP_SZ_BYTE*8);
   /*
   ** get the byte offset
   */
   byte_offset = bit_id/8;
   /*
   ** get the buffer index
   */
   buf_idx = byte_offset/EXPT_MAX_BUFFER_SZ;
   idx_in_buf = byte_offset%EXPT_MAX_BUFFER_SZ;
   /*
   ** check if the buffer is already allocated
   */
   if (buf_p->bitmap_buf[buf_idx] == NULL)
   {
     buf_p->bitmap_buf[buf_idx] = malloc(EXPT_MAX_BUFFER_SZ);
     if (buf_p->bitmap_buf[buf_idx] == NULL) goto out;
     memset(buf_p->bitmap_buf[buf_idx],0,EXPT_MAX_BUFFER_SZ);
   }
   bitmap = buf_p->bitmap_buf[buf_idx];
   /*
   ** assert the corresponding bit in the buffer
   */
   bitmap[idx_in_buf] |=(1<<(bit_id%8));
   p->last_time_change = time(NULL);
   buf_p->bitmap |=1<<buf_idx;
   expt_write_count++;

out:   
   if ((errno = pthread_rwlock_unlock(&p->lock)) != 0) {
       severe("can't unlock inode tracking entry %s : %s",p->name,strerror(errno));
       expt_write_error++;
       return -1;
   }
   return 0;
}
   
/*
**__________________________________________________________________
*/
/**
*   flush on disk

    @param root_path: pointer to the root pathname
    @param p: pointer to the bitmap structure to flush on disk
    
    @retval 0 on success
    @retval < 0 on error (see errno for details)
*/
int expt_disk_flush(expt_ctx_t *p)
{
   int fd = -1;
   int i;
   char pathname[1024];
   ssize_t ret;
   int status = -1;   
   char *bitmap=NULL;
   int idx_cur;
   expt_ctx_buf_t *buf_p;
   uint64_t *buf_read_p = NULL;

   /*
   ** check if there is a change
   */
   if ((p->buf[0].bitmap == 0)&& (p->buf[1].bitmap == 0))
   {
     /*
     ** no change
     */
     return 0;
   }
   buf_read_p = malloc(EXPT_MAX_BUFFER_SZ);
   if (buf_read_p == NULL) return 0;
   
   if ((errno = pthread_rwlock_wrlock(&p->lock)) != 0) {
       severe("can't lock inode tracking entry %s : %s",p->name,strerror(errno));
       goto out;
   }
   /*
   ** swap the buffers
   */
   idx_cur = p->cur_buf;
   p->cur_buf = (idx_cur+1)&0x1;
   /*
   ** release the lock
   */
   if ((errno = pthread_rwlock_unlock(&p->lock)) != 0) {
       severe("can't lock inode tracking entry %s : %s",p->name,strerror(errno));
       goto out;
   }
   /*
   ** there is a change, so open the file
   */
   sprintf(pathname,"%s/trk_bitmap",p->name);
   if ((fd = open(pathname, O_RDWR | O_CREAT , 0640)) < 0)  
   {
      severe("cannot open/create %s:%s",pathname,strerror(errno));
      goto out;
   }
   /*
   ** take the exclusive access to the file
   */
   ret = flock(fd,LOCK_EX);
   if (ret < 0)
   {
      severe("cannot lock %s:%s",pathname,strerror(errno));
      goto out;   
   }
   /*
   ** set the pointer to the current buffer set
   */
   buf_p = &p->buf[idx_cur]; 

   for (i = 0; i < EXPT_MAX_BUFFER; i++)
   {
     if ((buf_p->bitmap & (1<<i)) == 0) continue;
     /*
     ** attempt to read the data on disk
     */
     memset(buf_read_p,0,EXPT_MAX_BUFFER_SZ);
     ret = pread(fd,buf_read_p,EXPT_MAX_BUFFER_SZ,EXPT_MAX_BUFFER_SZ*i); 
     if (ret < 0)
     {
       severe("cannot read %s:%s",pathname,strerror(errno));
       goto out;               
     }
     bitmap = buf_p->bitmap_buf[i];
     if (ret != 0)
     {
        /*
	** merge the 2 buffers
	*/
	int k;
	uint64_t *p64;
	p64 = (uint64_t *)bitmap;
	for (k = 0; k < EXPT_MAX_BUFFER_SZ/8;k++) p64[k] |=buf_read_p[k];
     }     
     ret = pwrite(fd,bitmap,EXPT_MAX_BUFFER_SZ,EXPT_MAX_BUFFER_SZ*i);
     if (ret <0)
     {
       severe("cannot write %s:%s",pathname,strerror(errno));
       goto out;     
     }
     /*
     ** clear the buffer
     */
     memset(bitmap,0,EXPT_MAX_BUFFER_SZ);
     expt_write_bytes_count +=EXPT_MAX_BUFFER_SZ;
   }
   /*
   ** clear the bitmap
   */
   buf_p->bitmap = 0;
   status = 0;
   expt_write_th_rq_count++;
out:
   if (fd != -1) 
   {
     ret = flock(fd,LOCK_EX);
     if (ret < 0)
     {
	severe("cannot unlock %s:%s",pathname,strerror(errno));
     }     
     close(fd);
   }
   if (buf_read_p != NULL) free(buf_read_p);
   if (status < 0) expt_flush_error++;
   return status;
}  

/*
 *_______________________________________________________________________
 */
/** inode tracking thread
 */
 #define EXPT_PTHREAD_FREQUENCY_SEC 1
static void *expt_thread(void *v) {

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
     uma_dbg_thread_add_self("Meta_trk");
    int expt_thread_period_current_count = 0;
    expt_thread_period_count = 10;
    // Set the frequency of calls
    struct timespec ts = {EXPT_PTHREAD_FREQUENCY_SEC, 0};
   expt_ctx_t  *entry_p;
   int i;
    
    expt_thread_period_count = EXPT_PTHREAD_FREQUENCY_SEC*30;
    expt_poll_stats[0] = 0;
    expt_poll_stats[1] = 0;
    info("inode export tracking thread started ");

    for (;;) {
        expt_thread_period_current_count++;
	if (expt_thread_period_current_count >= expt_thread_period_count)
	{
          START_PROFILING_TH(expt_poll_stats);
          
          for (i = 0; i < expt_thread_cur_context;i++)
	  {
	     entry_p = expt_thread_ctx_p[i];
	     if (entry_p == NULL) continue;
	     expt_disk_flush(entry_p);
	  
	  
	  }
	  STOP_PROFILING_TH(expt_poll_stats);
	  expt_thread_period_current_count = 0;
	}
        nanosleep(&ts, NULL);
    }
    return 0;
}
/*
 *_______________________________________________________________________
 */
/**
*  init of the expt_thread

    @param none
    
*/
int expt_thread_init()
{
   if (expt_thread_init_done == 1) return 0;
  
   /*
   ** clear the pointers to the contexts
   */
   memset(expt_thread_ctx_p,0,sizeof(expt_ctx_t*[EXPT_MAX_TRACK_CTX]));
   expt_thread_max_context = EXPT_MAX_TRACK_CTX ;
   expt_thread_cur_context = 0;    

  /**
  * create the writeback thread
  */
  if ((errno = pthread_create(&exptctx_thread, NULL,
        expt_thread, NULL)) != 0) {
    severe("can't create inode tracking thread %s", strerror(errno));
    return -1;
  }

   uma_dbg_addTopic("inode_trck",show_expt_thread);
   expt_thread_init_done = 1;
   return 0;
   
}
