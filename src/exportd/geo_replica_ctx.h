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
 
#ifndef  GEO_REPLICA_CTX_H
#define GEO_REPLICA_CTX_H

#include <stdint.h>
#include <rozofs/rozofs.h>
#include <rozofs/core/ruc_list.h>
#include <rozofs/common/geo_replica_str.h>
#include "geo_replica_tmr.h"
#include "geo_replication.h"
typedef union _geo_local_ref_t
{
   uint32_t u32;
   struct {
   uint32_t timestamp:23;
   uint32_t index:9;   /**< index of the context */
   }s;
} geo_local_ref_t;

/**
*  structure used for synchronisation with client
*/

typedef struct _geo_proc_ctx_t
{
  ruc_obj_desc_t    link;          /* To be able to chain the MS context on any list */
  uint32_t            index;         /* Index of the MS */
  uint32_t            free;          /* Is the context free or allocated TRUE/FALSE */
  uint32_t            integrity;     /* the value of this field is incremented at  each MS ctx allocation */
/*
**   DO NOT MOVE THE EVENT/FLAG ARRAY: integrity field is used for giving
**   the address of the beginning of the bitfields
*/
  int eid;                         /**< export identifier                            */
  int site_id;                     /**< site identifier                              */
  int working_idx;                 /**< index of the allocated entry in working table */
  geo_local_ref_t local_ref;       /**< local reference: 31-24                       */
  uint32_t remote_ref;             /**< remote reference                             */
  uint64_t timestamp;              /**< last time the remote send a request          */
  uint64_t rate;                   /**< synchro file rate                            */
  geo_ctx_tmr_cell_t  rpc_guard_timer;   /**< guard timer associated with the synchro context */
  uint8_t  *record_buf_p;         /**< pointer to the content of the file in memory    */
  uint64_t  date;                 /**< create time of the file                         */
  uint32_t  recycle;              /**< assert to 1 when the file comes from the recycle list  */
  uint32_t  nb_records;           /**< number of records in the file                   */
  uint32_t  cur_record;           /**< current record index                            */
  uint64_t  file_idx;              /**< current synchro file index                     */

} geo_proc_ctx_t;

#define GEO_REP_SRV_CLI_CTX_MAX 512 /**< max number of contexts (shared among all the exports */
/**
* transaction statistics
*/
typedef enum 
{
  GEO_CTX_TIMEOUT=0,
  GEO_CTX_NO_CTX_ERROR,
  GEO_CTX_CTX_MISMATCH,
  GEO_CTX_COUNTER_MAX,
}geo_ctx_stats_e;

extern uint64_t geo_proc_stats[];
extern uint32_t geo_srv_timestamp;

#define GEO_CTX_STATS(counter) geo_proc_stats[counter]++;

/*
**______________________________________________________________________________
*/
/*
**  API coall upon a time-out on client context supervision

   @param entry_p : pointer to the client context

  @retval none
*/
void geo_rep_client_ctx_tmo(geo_proc_ctx_t *entry_p);
/*
**____________________________________________________
*/
static inline uint32_t geo_srv_timestamp_get()
{
  geo_srv_timestamp++;
  if (geo_srv_timestamp == 0) geo_srv_timestamp = 1;
  return geo_srv_timestamp;
}
/*
**____________________________________________________
*/
/**
   geo_proc_module_init

  create the geo-replication context pool

@param     : context_count : number of contexts
@retval   : 0 : done
@retval     -1 : out of memory
 */
int geo_proc_module_init(uint32_t context_count) ;

/*
**____________________________________________________
*/
/**
   geo_proc_alloc

   create a Transaction context
    That function tries to allocate a free PDP
    context. In case of success, it returns the
    index of the Transaction context.
 
    @param     : rnone

    @retval   : <>NULL: pointer to the allocated context
    @retval    NULL if out of context.
 */
geo_proc_ctx_t *geo_proc_alloc();
/*
**____________________________________________________
*/
/**
   delete a  context
   
   That function is intended to be called when
   a Transaction context is deleted. It returns the
   Transaction context to the free list. The delete
   procedure of the MS automaton and
   controller are called by that service.

   If the Transaction context is out of limit, and 
   error is returned.

   @param     : index of the context
   @retval   : RUC_OK : context has been deleted
   @retval     RUC_NOK : out of limit index.
 */
uint32_t geo_proc_free_from_idx(uint32_t context_id);
/*
**____________________________________________________
*/
/**
   geo_proc_free_from_ptr

   delete a  context

   If the  context is out of limit, and 
   error is returned.

   @param     : pointer to the context
   @retval   : RUC_OK : context has been deleted
   @retval     RUC_NOK : out of limit index.

*/
uint32_t geo_proc_free_from_ptr(geo_proc_ctx_t *p);
/*
**____________________________________________________
*/
/*
  start the guard timer associated with a context

  @param     : tx_p : pointer to the transaction context
  @param     : uint32_t  : delay in seconds (??)
  @retval   : none
*/
void geo_proc_start_timer(geo_proc_ctx_t *tx_p, uint32_t time_ms);

/*
**____________________________________________________
*/
/**
  stop the guard timer associated with the transaction

  @param     :  tx_p : pointer to the transaction context
  @retval   : none
*/
void geo_proc_stop_timer(geo_proc_ctx_t *pObj);
/*
**____________________________________________________
*/
/**
    geo_proc_getObjCtx_p

  based on the object index, that function
  returns the pointer to the object context.
 
  That function may fails if the index is
  not a Transaction context index type.
 
   @param     : MS index
   @retval   : NULL if error
 */
geo_proc_ctx_t *geo_proc_getObjCtx_p(uint32_t context_id);
#endif
