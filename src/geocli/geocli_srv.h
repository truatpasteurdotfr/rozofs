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
 
 #ifndef GEOCLI_SRV_H
 #define GEOCLI_SRV_H
 

#include <stdint.h>
#include <rozofs/rozofs.h>
 
 typedef enum 
{
  GEOCLI_ST_IDLE = 0,
  GEOCLI_ST_INPRG,
  GEOCLI_ST_MAX
} geocli_ctx_state_e;


typedef enum 
{
  GEOSYNC_ST_IDLE = 0,
  GEOSYNC_ST_SYNC_FILE,
  GEOSYNC_ST_GETNEXT,
  GEOSYNC_ST_GETDEL,
  GEOSYNC_ST_MAX
} geocli_sync_state_e;


extern int geocli_period;  

typedef struct _geo_cli_profiler_t {
  uint64_t geo_sync_req[5];
  uint64_t geo_sync_get_next_req[5];
  uint64_t geo_sync_delete_req[5];
  uint64_t geo_sync_close_req[5];
} geo_cli_profiler_t;


typedef struct _geocli_ctx_t
{
   uint64_t timestamp;
   geocli_ctx_state_e state;
   geocli_sync_state_e state_sync;
   uint64_t file_idx;
//   char filename[ROZOFS_FILENAME_MAX];
   uint16_t  eid;      /**< export identifier */
   uint16_t  site_id;   /**< local site identifier */
   uint32_t local_ref;
   uint32_t remote_ref;
   uint32_t last;
   uint32_t first_record;  /**< index of the first record in data_record */
   uint32_t nb_records;    /**< nb records in data record                */
   uint32_t cur_record;    /**< current index in data_record             */
   uint32_t data_record_len;
   char     *data_record;
   uint64_t status_bitmap;
   uint32_t delete_forbidden;  /**< asserted if there is a failure on a record */
   geo_cli_profiler_t profiler;
} geocli_ctx_t;   

#define GEO_IDX_COUNT 0
#define GEO_IDX_TMO 1
#define GEO_IDX_ERR 2
#define GEO_IDX_TIME 3
#define GEO_IDX_SAVE_TIME 4

#define START_GEO_CLI_PROFILING(the_probe)\
{\
    struct timeval tv;\
    p->profiler.the_probe[GEO_IDX_COUNT]++;\
    gettimeofday(&tv,(struct timezone *)0);\
    p->profiler.the_probe[GEO_IDX_SAVE_TIME] = MICROLONG(tv);\
}

#define STOP_GEO_CLI_PROFILING(the_probe,status)\
{\
    uint64_t toc;\
    struct timeval tv;\
    gettimeofday(&tv,(struct timezone *)0);\
    toc = MICROLONG(tv);\
	  switch (status) \
	  { \
	    case 0: \
	         break;\
	    case EAGAIN:\
	    p->profiler.the_probe[GEO_IDX_TMO]++;\
	    break;\
	    default:\
            if (geo_replica_log_enable) severe("%s:%s",#the_probe,strerror(errno));\
	    p->profiler.the_probe[GEO_IDX_ERR]++;\
	    break;\
	  }\
          p->profiler.the_probe[GEO_IDX_TIME] += (toc - p->profiler.the_probe[GEO_IDX_SAVE_TIME]);\
    }


extern int geo_replica_log_enable;
/*
**____________________________________________________
*/
/**init of the client part of the geo-replication

  @param period: geo replication period in ms
  @param eid: export identifier get from mount response
  @param site_id: site identifier
  
  @retval 0 on success
  @retval -1 on error
*/
int geocli_init(int period,uint16_t eid,uint16_t site_id);
 #define GEO_DEF_PERIO_MS 100
 #define GEO_SLOW_POLL_COUNT 100
 #define GEO_FAST_POLL_COUNT 1
 /*
**____________________________________________________
*/
/**
*  Start of a copy process for a bunch of files

   That function is intended be called after the
   reception of either a sync_req or a sync_getnext_req
   response

   - start the file synchro process
   
*/
void geo_cli_geo_file_sync_processing(geocli_ctx_t *p);
/*
**____________________________________________________
*/
 /**
 *   end of read event
 
    @param param: pointer to the copy context
    @param status: status of the operation : 0 success / < 0 error
    
    in case of error, errno contains the reason
    
    @retval none
*/
void geo_cli_geo_file_sync_read_end_cbk(void *param,int status);

/*
**____________________________________________________
*/
 /**
 *   end of write event
 
    @param param: pointer to the copy context
    @param status: status of the operation : 0 success / < 0 error
    
    in case of error, errno contains the reason
    
    @retval none
*/
void geo_cli_geo_file_sync_write_end_cbk(void *param,int status); 


#endif
