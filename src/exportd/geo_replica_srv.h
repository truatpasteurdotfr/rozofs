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
#ifndef GEO_REPLICA_SRV_H
#define GEO_REPLICA_SRV_H
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>
#include <sys/types.h>
#include <pthread.h>
#include <rozofs/rpc/export_profiler.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/rpc/geo_replica_proto.h>
#include "export.h"
#include "geo_replica_proto_nb.h"
#include "geo_replica_ctx.h"


#define GEO_REP_DEFAULT_GUARD_TMR (70)
typedef enum _geo_rep_work_e 
{
   GEO_WORK_FREE = 0, /**< the entry is free                                          */
   GEO_WORK_BUSY,    /**< the context has a valid file_idx and a valid synchro client */
   GEO_WORK_AVAIL,   /**< the context has a valid file_idx but no synchro client      */
   GEO_WORK

} geo_rep_work_e;


typedef enum _geo_rep_srv_err_e
{
  GEO_ERR_ALL_ERR = 0,
  GEO_ERR_SYNC_FILE_OPEN_ERR,
  GEO_ERR_SYNC_FILE_ENOENT,
  GEO_ERR_SYNC_FILE_READ_ERR,
  GEO_ERR_SYNC_FILE_READ_MISMATCH,
  GEO_ERR_SYNC_FILE_STAT_ERR,
  GEO_ERR_SYNC_FILE_UNLINK_ERR,
  GEO_ERR_SYNC_CTX_UNAVAILABLE,
  GEO_ERR_SYNC_CTX_TIMEOUT,
  GEO_ERR_SYNC_CTX_ERANGE,
  GEO_ERR_SYNC_CTX_MISMATCH,
  GEO_ERR_ROOT_CTX_UNAVAILABLE,
  GEO_ERR_IDX_FILE_READ_ERR,
  GEO_ERR_IDX_FILE_WRITE_ERR,
  GEO_ERR_IDX_FILE_RENAME_ERR,
  GEO_ERR_OUT_OF_MEMORY,
  GEO_ERR_MAX
} geo_rep_srv_err_e;

typedef struct _geo_srv_sync_err_t
{
   uint64_t     err_stats[GEO_ERR_MAX];
} geo_srv_sync_err_t; 


/**
* macro assocaited with the error statistics
*/
#define GEO_ERR_STATS(eid,site_id,errcode) \
{ geo_srv_sync_err_t *bid_ptr; \
  bid_ptr = &geo_srv_sync_err_tab_p[(eid<<EXPORT_GEO_MAX_BIT) | (site_id &(EXPORT_GEO_MAX_CTX-1))]; \
  bid_ptr->err_stats[errcode]++;\
  bid_ptr->err_stats[GEO_ERR_ALL_ERR]++;\
  if (geo_replica_log_enable) severe("%s at eid %d site %d:%s",#errcode,eid,site_id,strerror(errno));\
}
/**
*  that structure is used for tracking the files for which there is a 
*  synchronisation in progress 
*/

typedef struct _geo_rep_work_t
{
   uint32_t state;              /**< see enum above                                                      */
   geo_local_ref_t local_ref;  /**< reference of the context associated with the current working context */
   uint64_t file_idx ;         /**< file index under synchronization                                     */
} geo_rep_work_t;

#define GEO_MAX_SYNC_WORKING  32

typedef struct _geo_srv_sync_ctx_t
{
   geo_rep_srv_ctx_t  parent;  
   int                nb_working_cur;  /**< current number of busy/available entries  */
   geo_rep_work_t     working[GEO_MAX_SYNC_WORKING];
} geo_srv_sync_ctx_t; 


typedef struct _geo_srv_sync_ctx_tab_t
{
  geo_srv_sync_ctx_t *site_table_p[EXPORT_GEO_MAX_CTX];
} geo_srv_sync_ctx_tab_t;
 
 
extern geo_srv_sync_ctx_tab_t *geo_srv_sync_ctx_tab_p[];
extern geo_srv_sync_err_t geo_srv_sync_err_tab_p[];
extern uint32_t geo_rep_guard_timer_ms; 
extern int geo_replica_log_enable;
/*
**______________________________________________________________________________
*/
/**
  Init of the RPC server side for geo-replication
  
   @param  args: exportd start arguments
   
   @retval 0 on success
   @retval -1 on error (see errno for details)
   
*/
int geo_replicat_rpc_srv_init(void *args);
/*
**______________________________________________________________________________
*/
/**
*   geo-replication: attempt to get some file to replicate

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    @param local_ref :local reference of the caller
    @param rsp: pointer to the response context
    
   @retval 0 on success
   @retval -1 on error see errno for details 

*/
int geo_replicat_sync_req(export_t * e,uint16_t site_id,uint32_t local_ref,
                          geo_sync_data_ret_t *rsp);

/*
**______________________________________________________________________________
*/
/**
*   geo-replication: get the next records of the file for which the client
    get a positive acknowledgement for file synchronization

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    @param local_ref :local reference of the caller
    @param remote_ref :reference provided by the serveur
    @param next_record : nex record to get (first)
    @param file_idx :index of the geo-synchro file
    @param status_bitmap :64 entries: 1: failure / 0: success
    @param rsp: pointer to the response context
    
   @retval 0 on success
   @retval -1 on error see errno for details 
*/
int geo_replicat_get_next(export_t * e,uint16_t site_id,
                          uint32_t local_ref,uint32_t remote_ref,
			  uint32_t next_record,uint64_t file_idx,
			  uint64_t status_bitmap,
			  geo_sync_data_ret_t *rsp);

/*
**______________________________________________________________________________
*/
/**
*   geo-replication: ask for file deletion upon end of synchronization of file

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    @param local_ref :local reference of the caller
    @param remote_ref :reference provided by the serveur
    @param filename :index of the geo-synchro file
    
   @retval 0 on success
   @retval -1 on error see errno for details 

*/
int geo_replicat_delete(export_t * e,uint16_t site_id,
                          uint32_t local_ref,uint32_t remote_ref,
			  uint64_t file_idx);



/*
**______________________________________________________________________________
*/
/**
*   geo-replication: ask for file abort

    @param e: pointer to the export data structure
    @param site_id : source site identifier
    @param local_ref :local reference of the caller
    @param remote_ref :reference provided by the serveur
    @param file_idx :index of the geo-synchro file
    @param status_bitmap :64 entries: 1: failure / 0: success
    
   @retval 0 on success
   @retval -1 on error see errno for details 

*/
int geo_replicat_close(export_t * e,uint16_t site_id,
                          uint32_t local_ref,uint32_t remote_ref,
			  uint64_t file_idx,
			  uint64_t status_bitmap );			  

/*
**______________________________________________________________________________
*/
/**
*   Get the context associated with an eid and a site_id
    If the context is not found a fresh one is allocated

    @param eid: export identifier
    @param site_id : source site identifier
    
    @retval <> NULL: pointer to the associated context
    @retval NULL: out of memory
*/
geo_srv_sync_ctx_t *geo_srv_get_eid_site_context_ptr(int eid,uint16_t site_id);
#endif			  
