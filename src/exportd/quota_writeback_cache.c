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


#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/core/uma_dbg_api.h>
#include "rozofs_quota.h"

/*
** pointer to the dirent write back cache
*/
quota_wbcache_entry_t   *quota_wbcache_cache_p = NULL;
int quota_wbcache_cache_initialized = 0;
int quota_wbcache_cache_enable = 0;
uint64_t quota_wbcache_hit_counter = 0;
uint64_t quota_wbcache_miss_counter = 0;
uint64_t quota_wbcache_write_bytes_count = 0;
uint64_t quota_wbcache_flush_counter = 0;
uint64_t quota_wb_total_chunk_write_cpt = 0;

uint64_t quota_wbcache_unlock_err = 0;
uint64_t quota_wbcache_lock_write_err = 0;
uint64_t quota_wbcache_disk_write_err = 0;

/*
** read statistics
*/
uint64_t quota_wbcache_read_bytes_count =0;
uint64_t quota_wbcache_read_count = 0;
uint64_t quota_wbcache_disk_read_bytes_count =0;
uint64_t quota_wbcache_disk_read_count = 0;
uint64_t quota_wbcache_disk_read_err = 0;
uint64_t quota_wbcache_lock_read_err = 0;

int quota_wbcache_thread_period_count;
uint64_t quota_wbcache_poll_stats[2];
 uint64_t quota_wbcache_th_write_bytes_count = 0;
 uint64_t quota_wb_write_th_count = 0;
 uint64_t quota_wbcache_disk_write_bytes_count = 0;
 
static pthread_t quota_wbcache_ctx_thread;
int      export_tracking_thread_period_count;   /**< current period in seconds  */
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

static char * show_wbcache_thread_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"quota_wbthread reset                : reset statistics\n");
  pChar += sprintf(pChar,"quota_wbthread disable              : disable writeback dirent cache\n");
  pChar += sprintf(pChar,"quota_wbthread enable               : enable writeback dirent cache\n");
  pChar += sprintf(pChar,"quota_wbthread period [ <period> ]  : change thread period(unit is second)\n");  
  return pChar; 
}
/*
**_______________________________________________________________
*/
char * show_wbcache_quota_thread_stats_display(char *pChar)
{
     /*
     ** display the statistics of the thread
     */
     pChar += sprintf(pChar,"period     : %d second(s) \n",quota_wbcache_thread_period_count);
     pChar += sprintf(pChar,"statistics :\n");
     pChar += sprintf(pChar," - wr chunk counter  :%llu\n",
              (long long unsigned int)quota_wb_total_chunk_write_cpt);
     pChar += sprintf(pChar," - write (hit/miss)  :%llu/%llu\n",
              (long long unsigned int)quota_wbcache_hit_counter,
              (long long unsigned int)quota_wbcache_miss_counter);


     /*
     ** read and clear counters update
     */
     quota_wbcache_hit_counter       = 0;
     quota_wbcache_miss_counter      = 0;
     quota_wb_total_chunk_write_cpt  = 0;



     pChar += sprintf(pChar," - activation counter:%llu\n",
              (long long unsigned int)quota_wbcache_poll_stats[P_COUNT]);
     pChar += sprintf(pChar," - average time (us) :%llu\n",
                      (long long unsigned int)(quota_wbcache_poll_stats[P_COUNT]?
		      quota_wbcache_poll_stats[P_ELAPSE]/quota_wbcache_poll_stats[P_COUNT]:0));
     pChar += sprintf(pChar," - total time (us)   :%llu\n",(long long unsigned int)quota_wbcache_poll_stats[P_ELAPSE]);
     pChar+=sprintf(pChar,"total Write            : memory %llu MBytes (%llu Bytes) requests %llu\n\n",
             (long long unsigned int) quota_wbcache_th_write_bytes_count / (1024*1024),
             (long long unsigned int) quota_wbcache_th_write_bytes_count,
             (long long unsigned int) quota_wb_write_th_count);
     return pChar;


}



/**
*____________________________________________________________
*/
/*static inline */char *quota_wbcache_display_stats(char *pChar) {
    pChar+=sprintf(pChar,"Quota WriteBack cache statistics:\n");
    pChar+=sprintf(pChar,"state          :%s\n",(quota_wbcache_cache_enable==0)?"Disabled":"Enabled");
    pChar+=sprintf(pChar,"NB entries     :%d\n",QUOTA_CACHE_MAX_ENTRY);
    pChar+=sprintf(pChar,"Read statistics\n");
    pChar+=sprintf(pChar,"  hit/miss      : %llu/%llu\n",
            (long long unsigned int) quota_wbcache_read_count,
            (long long unsigned int) quota_wbcache_disk_read_count);
    pChar+=sprintf(pChar,"Write statistics\n");
    pChar+=sprintf(pChar,"  hit/miss/flush : %llu/%llu/%llu\n",
            (long long unsigned int) quota_wbcache_hit_counter,
            (long long unsigned int) quota_wbcache_miss_counter,
            (long long unsigned int) quota_wbcache_flush_counter
	    );

    pChar+=sprintf(pChar,"Error stats\n");
    pChar+=sprintf(pChar,"  lock read    : %llu\n",(long long unsigned int) quota_wbcache_lock_read_err);    
    pChar+=sprintf(pChar,"  lock write   : %llu\n",(long long unsigned int) quota_wbcache_lock_write_err); 
    pChar+=sprintf(pChar,"  unlock       : %llu\n",(long long unsigned int) quota_wbcache_unlock_err);	
    pChar+=sprintf(pChar,"  read disk    : %llu\n",(long long unsigned int) quota_wbcache_disk_read_err);	
    pChar+=sprintf(pChar,"  write disk   : %llu\n",(long long unsigned int) quota_wbcache_disk_write_err);	
        
    pChar+=sprintf(pChar,"total Write\n");
    pChar+=sprintf(pChar,"  direct : memory %llu MBytes (%llu Bytes) %llu chunks flushed\n",
            (long long unsigned int) quota_wbcache_write_bytes_count / (1024*1024),
            (long long unsigned int) quota_wbcache_th_write_bytes_count,
            (long long unsigned int) quota_wbcache_flush_counter
             );

    pChar+=sprintf(pChar,"  thread : memory %llu MBytes (%llu Bytes) requests %llu flushed %llu\n",
            (long long unsigned int) quota_wbcache_write_bytes_count / (1024*1024),
            (long long unsigned int) quota_wbcache_write_bytes_count,
            (long long unsigned int) quota_wb_total_chunk_write_cpt,
	    (long long unsigned int) quota_wb_write_th_count);
    return pChar;
}


/**
*  Get the hash idx for the writeback cache

   @param fid
   
   @retval index in the writeback cache
*/
static inline int quota_wbcache_get_index(void *key)
{

    uint32_t h;

    unsigned char *d = (unsigned char *) key;
    int i = 0;

    h = 2166136261U;
    /*
     ** hash on name
     */
    for (i=0; i < sizeof(uint64_t); d++, i++) {
        h = (h * 16777619)^ *d;

    }
    return (int)(h%QUOTA_CACHE_MAX_ENTRY);
}
/*
**_______________________________________________________________
*/
/**
* check if there is some write pending for the cache entry

  @param cache_p : pointer to the cache entry
  
  @retval 1 : write pending
  @retval 0 : no write pending
*/ 
static inline int quota_wbcache_check_write_pending(quota_wbcache_entry_t  *cache_p)
{
   int i;
   for (i = 0; i < QUOTA_CACHE_MAX_CHUNK; i++)
   {
      if (cache_p->chunk[i].wr_cpt != 0) return 1;
   }
   return 0;
 }
/*
**_______________________________________________________________
*/
void show_wbcache_quota_thread(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();
    int ret;
    int period;
    
    
    if (argv[1] == NULL) {
      show_wbcache_quota_thread_stats_display(pChar);
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer()); 
      return;  	  
    }

    if (strcmp(argv[1],"reset")==0) {
      pChar = show_wbcache_quota_thread_stats_display(pChar);
      pChar +=sprintf(pChar,"\nStatistics have been cleared\n");
      quota_wbcache_poll_stats[0] = 0;
      quota_wbcache_poll_stats[1] = 0;
      uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());  
      return;   
    }
    if (strcmp(argv[1],"period")==0) {   
	if (argv[2] == NULL) {
	show_wbcache_thread_help(pChar);	
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());   
	return;  	  
      }
      ret = sscanf(argv[2], "%d", &period);
      if (ret != 1) {
	show_wbcache_thread_help(pChar);	
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
      
      quota_wbcache_thread_period_count = period;
      uma_dbg_send(tcpRef, bufRef, TRUE, "Done\n");   	  
      return;
    }
    if (strcmp(argv[1],"disable")==0) {   
	quota_wbcache_cache_enable = 0;
	uma_dbg_send(tcpRef, bufRef, TRUE,"dirent writeback cache is disabled");   
	return;   
    }
    if (strcmp(argv[1],"enable")==0) {   
	quota_wbcache_cache_enable = 1;
	uma_dbg_send(tcpRef, bufRef, TRUE,"dirent writeback cache is enabled");   
	return;   
    }
    show_wbcache_thread_help(pChar);	
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());    
    return;
}


/*
 *_______________________________________________________________________
 */
/**
* flush the content of a cache entry on disk

  @param cache_p: pointer to the cache entry
*/
int quota_wbcache_diskflush(quota_wbcache_entry_t  *cache_p)
{
  quota_chunk_cache_t *chunk_p;
  int i;
  int status = -1;
  int ret;
  rozofs_dquot_t *p;
  /*
  ** take the lock associated with the entry
  */
  if ((errno = pthread_rwlock_wrlock(&cache_p->lock)) != 0) {
      severe("can't lock writeback cache entry: %s", strerror(errno));
      quota_wbcache_lock_write_err++;
      return -1;
  }
  /*
  ** go through the chunk
  */
  chunk_p = &cache_p->chunk[0];
  for (i = 0; i < QUOTA_CACHE_MAX_CHUNK; i++,chunk_p++)
  {
     if (chunk_p->wr_cpt == 0) continue;
     p = chunk_p->chunk_p;
     ret = disk_tb_write_entry(chunk_p->ctx_p,p->key.s.qid,chunk_p->chunk_p);
     if (ret < 0)
     {
	severe ("QUOTA_WB write error :%s",strerror(errno));
	quota_wbcache_disk_write_err++;
     }  
     chunk_p->wr_cpt = 0; 
     quota_wbcache_th_write_bytes_count+= chunk_p->size;
     quota_wb_write_th_count++;
  }
  /*
  ** clear the header write pointer here to avoid race condition with check for flush
  */
  status = 0;
  if ((errno = pthread_rwlock_unlock(&cache_p->lock)) != 0) {
      severe("can't lock writeback cache entry: %s", strerror(errno));
      quota_wbcache_unlock_err++;
      return -1;
  } 
  return status;   

}
/*
 *_______________________________________________________________________
 */
/** writeback thread
 */
 #define DIRENT_WBCACHE_PTHREAD_FREQUENCY_SEC 1
static void *quota_wbcache_thread(void *v) {

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    int quota_wbcache_thread_period_current_count = 0;
    quota_wbcache_thread_period_count = 1;
    // Set the frequency of calls
    struct timespec ts = {DIRENT_WBCACHE_PTHREAD_FREQUENCY_SEC, 0};
   quota_wbcache_entry_t  *cache_p;
   int i;
    
    quota_wbcache_thread_period_count = DIRENT_WBCACHE_PTHREAD_FREQUENCY_SEC;
    quota_wbcache_poll_stats[0] = 0;
    quota_wbcache_poll_stats[1] = 0;
    info("WriteBack cache thread started ");

    for (;;) {
        quota_wbcache_thread_period_current_count++;
	if (quota_wbcache_thread_period_current_count >= quota_wbcache_thread_period_count)
	{
          START_PROFILING_TH(quota_wbcache_poll_stats);
          cache_p = &quota_wbcache_cache_p[0];
          for (i = 0; i < QUOTA_CACHE_MAX_ENTRY;i++,cache_p++)
	  {
	     if (cache_p->state == 0) continue;
	     if (quota_wbcache_check_write_pending(cache_p) == 0)
	     {
	        continue;
	     }
	     quota_wbcache_diskflush(cache_p);
	  
	  
	  }
	  STOP_PROFILING_TH(quota_wbcache_poll_stats);
	  quota_wbcache_thread_period_current_count = 0;
	}
        nanosleep(&ts, NULL);
    }
    return 0;
}

/**
*____________________________________________________________
*/
/**
*  attempt to get the quota from write back cache, otherwise need to
   read it from disk
   
   @param disk_p : disk table context
   @param buf: buffer to write
   @param count : length to write

*/
int quota_wbcache_read(disk_table_header_t  *disk_p,rozofs_dquot_t *buf,int count)
{
  quota_wbcache_entry_t  *cache_p;
  quota_chunk_cache_t       *chunk_p;
  int i;
  int ret;

  /*
  ** get the cache entry
  */
  i = quota_wbcache_get_index(&buf->key.u64);
  cache_p = &quota_wbcache_cache_p[i];
  /*
  ** lock the cache entry before storing the data
  */ 
  if ((errno = pthread_rwlock_wrlock(&cache_p->lock)) != 0) {
      quota_wbcache_lock_read_err++;
      severe("can't lock writeback cache entry: %s", strerror(errno));
      return -1;
  }
  chunk_p = cache_p->chunk;
  for (i = 0; i < QUOTA_CACHE_MAX_CHUNK; i++)
  {
     /*
     ** search for the same quota identifier
     */
     if (chunk_p[i].key == buf->key.u64)
     {
        /*
	** there is a match
	*/
	if (chunk_p[i].chunk_p == NULL)
	{
           /*
	   ** no buffer need to read from disk
	   */ 
	   break;
	} 
	memcpy(buf,chunk_p[i].chunk_p,count);
        quota_wbcache_read_bytes_count+=count;
        quota_wbcache_read_count++;
	goto out;      
     }
  }
  /*
  ** not found : need to read it from disk
  */
  ret = disk_tb_read_entry(disk_p,buf->key.s.qid,buf);
  if (ret < 0) 
  {
     quota_wbcache_disk_read_err++;
     goto error;
  }
  quota_wbcache_disk_read_bytes_count+=count;
  quota_wbcache_disk_read_count++;

out :  
  /*
  ** unlock the cache entry before storing the data
  */ 
  if ((errno = pthread_rwlock_unlock(&cache_p->lock)) != 0) {
      severe("can't lock writeback cache entry: %s", strerror(errno));
      quota_wbcache_unlock_err++;
      return -1;
  }
  return count;

error:
  count = -1;
  goto  out;

}

/*
 *_______________________________________________________________________
 */
/**
* local API to flush on disk a chunk to make room for a new one
  The lock is already taken at the time this service is called

  @param cache_p: pointer to the cache entry
*/
int quota_wbcache_chunk_diskflush(quota_wbcache_entry_t  *cache_p)
{
  quota_chunk_cache_t *chunk_p;
  int status = -1;
  rozofs_dquot_t *p;
  int ret;
  
  chunk_p = &cache_p->chunk[0];
  p=chunk_p->chunk_p;
  ret = disk_tb_write_entry(chunk_p->ctx_p,p->key.s.qid,p);
  if (ret < 0)
  {
     quota_wbcache_disk_write_err++;
     severe ("QUOTA_WB write error :%s",strerror(errno));
     goto error;
  }
  chunk_p->wr_cpt = 0;      
  quota_wbcache_disk_write_bytes_count+= chunk_p->size;
  quota_wbcache_flush_counter++;
  status = 0;
error:
  return status;   

}
/**
*____________________________________________________________
*/
/**
*  write the quota in the writeback cache

   @param disk_p : disk table context
   @param buf: buffer to write
   @param count : length to write
   
   @retval >= 0 : number of bytes written in the writeback cache
   @retval < 0 : error see errno for details
*/
int quota_wbcache_write(disk_table_header_t  *disk_p,rozofs_dquot_t *buf,int count)
{
  quota_wbcache_entry_t  *cache_p;
  quota_chunk_cache_t       *chunk_p;
  int i;
  int free_chunk = -1;
  int ret;

  /*
  ** get the cache entry
  */
  i = quota_wbcache_get_index(&buf->key.u64);
  cache_p = &quota_wbcache_cache_p[i];
  /*
  ** lock the cache entry before storing the data
  */ 
  if ((errno = pthread_rwlock_wrlock(&cache_p->lock)) != 0) {
      severe("can't lock writeback cache entry: %s", strerror(errno));
      return -1;
  }
  quota_wb_total_chunk_write_cpt++;
  chunk_p = cache_p->chunk;
  free_chunk = -1;
  for (i = 0; i < QUOTA_CACHE_MAX_CHUNK; i++)
  {
     /*
     ** search for the same quota identifier
     */
     if (chunk_p[i].key == buf->key.u64)
     {
        /*
	** there is a match
	*/
	if (chunk_p[i].chunk_p == NULL)
	{
           chunk_p[i].chunk_p = malloc(count);
	   if (chunk_p[i].chunk_p== NULL) goto error;
	} 
	memcpy(chunk_p[i].chunk_p,buf,count);
        chunk_p[i].ctx_p = disk_p;
	chunk_p[i].size = count;
        quota_wbcache_write_bytes_count+=count;
        quota_wbcache_hit_counter++;
        chunk_p[i].wr_cpt+=1;

	goto out;      
     }
     if (((chunk_p[i].key == 0)|| (chunk_p[i].wr_cpt==0)) && ( free_chunk == -1))
     {
       free_chunk = i;
     }
  }
  /*
  ** not found : check if there is a free chunk
  */
reloop:
  if (free_chunk >= 0)
  {
    if (chunk_p[free_chunk].chunk_p == NULL)
    {
       chunk_p[free_chunk].chunk_p = malloc(count);
       if (chunk_p[free_chunk].chunk_p== NULL) return -1;
    } 
    memcpy(chunk_p[free_chunk].chunk_p,buf,count);
    chunk_p[free_chunk].key = buf->key.u64;
    chunk_p[free_chunk].ctx_p = disk_p;
    chunk_p[free_chunk].size = count;
    quota_wbcache_write_bytes_count+=count;
    quota_wbcache_miss_counter++;
    chunk_p[free_chunk].wr_cpt+=1;
    goto out;     
  }
  /*
  ** need to flush one chunk in order to get some room: always flush chunk 0
  */
  ret = quota_wbcache_chunk_diskflush(cache_p);
  if (ret < 0) goto error;
  /*
  ** fill the entry in chunk 0
  */
  free_chunk = 0;
  goto reloop;
  
out :  
  /*
  ** unlock the cache entry before storing the data
  */ 
  cache_p->state = 1;
  if ((errno = pthread_rwlock_unlock(&cache_p->lock)) != 0) {
      severe("can't lock writeback cache entry: %s", strerror(errno));
      return -1;
  }
  return count;

error:
  count = -1;
  goto  out;
}

/**
*____________________________________________________________
*/
/**
* init of the QUOTA write back cache

  @retval 0 on success
  @retval < 0 error (see errno for details)
*/

int quota_wbcache_init()
{
  int i;

  if (quota_wbcache_cache_initialized) return 0;
  quota_wbcache_cache_p = malloc(sizeof(quota_wbcache_entry_t)*QUOTA_CACHE_MAX_ENTRY);
  if (quota_wbcache_cache_p != NULL)
  {
    memset(quota_wbcache_cache_p,0,sizeof(quota_wbcache_entry_t)*QUOTA_CACHE_MAX_ENTRY);
    quota_wbcache_cache_initialized = 1;
    quota_wbcache_cache_enable = 1;
  }
  for (i = 0; i < QUOTA_CACHE_MAX_ENTRY; i++) 
  {
    if (pthread_rwlock_init(&quota_wbcache_cache_p[i].lock, NULL) != 0) {
        return -1;
    }    
  }
  /**
  * create the writeback thread
  */
  if ((errno = pthread_create(&quota_wbcache_ctx_thread, NULL,
        quota_wbcache_thread, NULL)) != 0) {
    severe("can't create writeback cache thread %s", strerror(errno));
    return -1;
  }
  return  0;

}
