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

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <rozofs/common/log.h>
#include <rozofs/core/ruc_list.h>
#include <rozofs/core/ruc_buffer_debug.h>
#include <rozofs/core/uma_dbg_api.h>
#include "geo_replica_tmr.h"
#include "geo_replica_ctx.h"

geo_proc_ctx_t *geo_proc_context_freeListHead; /**< head of list of the free context  */
geo_proc_ctx_t geo_proc_context_activeListHead; /**< list of the active context     */

uint32_t geo_proc_context_count; /**< Max number of contexts    */
uint32_t geo_proc_context_allocated; /**< current number of allocated context        */
geo_proc_ctx_t *geo_proc_context_pfirst; /**< pointer to the first context of the pool */
uint32_t geo_proc_global_context_id = 0;

uint64_t geo_proc_stats[GEO_CTX_COUNTER_MAX];
uint32_t geo_srv_timestamp=1;


#define MICROLONG(time) ((unsigned long long)time.tv_sec * 1000000 + time.tv_usec)
#define GEO_CTX_DEBUG_TOPIC      "geo_ctx"

/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void geo_proc_debug_show(uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();

    pChar += sprintf(pChar, "number of transaction contexts (initial/allocated) : %u/%u\n", geo_proc_context_count, geo_proc_context_allocated);
    pChar += sprintf(pChar, "context size (bytes)                               : %u\n", (unsigned int) sizeof (geo_proc_ctx_t));
    ;
    pChar += sprintf(pChar, "Total memory size (bytes)                          : %u\n", (unsigned int) sizeof (geo_proc_ctx_t) * geo_proc_context_count);
    ;
    pChar += sprintf(pChar, "Statistics\n");
    pChar += sprintf(pChar, "TIMEOUT        : %10llu\n", (unsigned long long int) geo_proc_stats[GEO_CTX_TIMEOUT]);
    pChar += sprintf(pChar, "CTX_MISMATCH   : %10llu\n", (unsigned long long int) geo_proc_stats[GEO_CTX_CTX_MISMATCH]);
    pChar += sprintf(pChar, "NO_CTX_ERROR   : %10llu\n", (unsigned long long int) geo_proc_stats[GEO_CTX_NO_CTX_ERROR]);
    pChar += sprintf(pChar, "\n");
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());

}

/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void geo_proc_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
    geo_proc_debug_show(tcpRef, bufRef);
}

/*__________________________________________________________________________
  Register to the debug SWBB
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void geo_proc_debug_init() {
    uma_dbg_addTopic(GEO_CTX_DEBUG_TOPIC, geo_proc_debug);
}

/*
 **  END OF DEBUG
 */

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
geo_proc_ctx_t *geo_proc_getObjCtx_p(uint32_t context_id) {
    uint32_t index;
    geo_proc_ctx_t *p;

    /*
     **  Get the pointer to the context
     */
    index = context_id & RUC_OBJ_MASK_OBJ_IDX;
    if (index >= geo_proc_context_count) {
        /*
         ** the MS index is out of range
         */
        severe( "geo_proc_getObjCtx_p(%d): index is out of range, index max is %d", index, geo_proc_context_count );
        return (geo_proc_ctx_t*) NULL;
    }
    p = (geo_proc_ctx_t*) ruc_objGetRefFromIdx((ruc_obj_desc_t*) geo_proc_context_freeListHead,
            index);
    return ((geo_proc_ctx_t*) p);
}
/*
**____________________________________________________
*/
/**
    geo_proc_getObjCtx_ref

  based on the object index, that function
  returns the pointer to the object context.
 
  That function may fails if the index is
  not a Transaction context index type.
 
  @param     : pointer to the context
  @retval   : >= 0: index of the context
  @retval  <0 : error

 */
uint32_t geo_proc_getObjCtx_ref(geo_proc_ctx_t *p) {
    uint32_t index;
    index = (uint32_t) (p - geo_proc_context_pfirst);
    index -= 1;

    if (index >= geo_proc_context_count) {
        /*
         ** the MS index is out of range
         */
        severe( "geo_proc_getObjCtx_p(%d): index is out of range, index max is %d", index, geo_proc_context_count );
	errno = ERANGE;
        return (uint32_t) - 1;
    }
    return index;
}
/*
**____________________________________________________
*/
/**
   geo_proc_init

  initialize the management module

@param     : NONE
@retval   none   :
 */
void geo_proc_init() {
    geo_proc_context_pfirst = (geo_proc_ctx_t*) NULL;

    geo_proc_context_allocated = 0;
    geo_proc_context_count = 0;
}
/*
**____________________________________________________
*/
/**
   geo_proc_ctxInit

  create the  context pool

@param     : pointer to the context
@retval   : none
 */
void geo_proc_ctxInit(geo_proc_ctx_t *p, uint8_t creation) {

    p->integrity = -1; /* the value of this field is incremented at 
					      each MS ctx allocation */

    p->timestamp = 0;
    p->eid = 0;
    p->site_id = 0;
    p->remote_ref = 0;
    p->local_ref.u32 = 0;
    p->date = 0;
    p->nb_records = 0;
    p->cur_record = 0;
    /* 
     ** timer cell
     */
    ruc_listEltInit((ruc_obj_desc_t *) & p->rpc_guard_timer);
}
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
geo_proc_ctx_t *geo_proc_alloc() {
    geo_proc_ctx_t *p;

    /*
     **  Get the first free context
     */
    if ((p = (geo_proc_ctx_t*) ruc_objGetFirst((ruc_obj_desc_t*) geo_proc_context_freeListHead))
            == (geo_proc_ctx_t*) NULL) {
        /*
         ** out of Transaction context descriptor try to free some MS
         ** context that are out of date 
         */
        GEO_CTX_STATS(GEO_CTX_NO_CTX_ERROR);
        severe( "out of context" );
        return NULL;
    }
    /*
     **  reinitilisation of the context
     */
    geo_proc_ctxInit(p, FALSE);
    p->local_ref.s.timestamp = geo_srv_timestamp_get();
    p->local_ref.s.index = p->index;
    if (p->record_buf_p != NULL)
    {
      free(p->record_buf_p);
      p->record_buf_p = NULL;
    }
    /*
     ** remove it for the linked list
     */
    geo_proc_context_allocated++;
    p->free = FALSE;

    ruc_objRemove((ruc_obj_desc_t*) p);

    return p;
}
/*
**____________________________________________________
*/
/**
   geo_proc_createIndex

  create a  context given by index 
   That function tries to allocate a free context. 
   In case of success, it returns the index of the context.
 
@param     : context_id is the reference of the context
@retval   : MS controller reference (if OK)
retval     -1 if out of context.
 */
uint32_t geo_proc_createIndex(uint32_t context_id) {
    geo_proc_ctx_t *p;

    /*
     **  Get the first free context
     */
    p = geo_proc_getObjCtx_p(context_id);
    if (p == NULL) {
        severe( "MS ref out of range: %u", context_id );
        return RUC_NOK;
    }
    /*
     ** return an error if the context is not free
     */
    if (p->free == FALSE) {
        severe( "the context is not free : %u", context_id );
        return RUC_NOK;
    }
    /*
     **  reinitilisation of the context
     */
    geo_proc_ctxInit(p, FALSE);
    /*
     ** remove it for the linked list
     */
    geo_proc_context_allocated++;


    p->free = FALSE;
    ruc_objRemove((ruc_obj_desc_t*) p);

    return RUC_OK;
}
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
uint32_t geo_proc_free_from_idx(uint32_t context_id) {
    geo_proc_ctx_t *p;

    if (context_id >= geo_proc_context_count) {
        /*
         ** index is out of limits
         */
        return RUC_NOK;
    }
    /*
    ** get the reference from idx
    */
    p = geo_proc_getObjCtx_p(context_id);
    /*
    ** stop the timer
    */
    geo_proc_stop_timer(p);
    /*
     ** remove it from the active list
     */
    ruc_objRemove((ruc_obj_desc_t*) p);
    /*
    ** clear the local reference and remote reference
    */
    p->local_ref.u32 = 0;
    p->remote_ref = 0;
    if (p->record_buf_p != NULL)
    {
      free(p->record_buf_p);
      p->record_buf_p = NULL;
    }
    /*
     **  insert it in the free list
     */
    geo_proc_context_allocated--;
    p->free = TRUE;
    ruc_objInsertTail((ruc_obj_desc_t*) geo_proc_context_freeListHead,
            (ruc_obj_desc_t*) p);

    return RUC_OK;
}
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
uint32_t geo_proc_free_from_ptr(geo_proc_ctx_t *p) {
    uint32_t context_id;

    context_id = geo_proc_getObjCtx_ref(p);
    if (context_id == (uint32_t) - 1) {
        GEO_CTX_STATS(GEO_CTX_CTX_MISMATCH);
        return RUC_NOK;
    }
    return (geo_proc_free_from_idx(context_id));
}
/*
**____________________________________________________
*/
/*
    Timeout call back associated with a context

 @param     :  tx_p : pointer to the transaction context
*/

void geo_proc_timeout_CBK(void *opaque) {
    geo_proc_ctx_t *pObj = (geo_proc_ctx_t*) opaque;

    /*
    ** Process the current time-out for that transaction
    */
    /*
    ** Update global statistics
    */
    GEO_CTX_STATS(GEO_CTX_TIMEOUT);
    /*
    ** release the context
    */
    geo_rep_client_ctx_tmo(pObj);
    
//    (*(pObj->recv_cbk))(pObj, pObj->user_param);
}
/*
**____________________________________________________
*/
/*
  stop the guard timer associated with the transaction

  @param     :  tx_p : pointer to the transaction context
  @retval   : none
*/
void geo_proc_stop_timer(geo_proc_ctx_t *pObj) {

    geo_ctx_tmr_stop(&pObj->rpc_guard_timer);
}
/*
**____________________________________________________
*/
/*
  start the guard timer associated with a context

  @param     : tx_p : pointer to the transaction context
  @param     : uint32_t  : delay in seconds (??)
  @retval   : none
*/
void geo_proc_start_timer(geo_proc_ctx_t *tx_p, uint32_t time_ms) {
    uint8_t slot;
    /*
     ** check if the context is still allocated, it might be possible
     ** that the receive callback of the application can be called before
     ** the application starts the timer, in that case we must
     ** prevent the application to start the timer
     */
    if (tx_p->free == TRUE) {
        /*
         ** context has been release
         */
        return;
    }
    /*
     **  remove the timer from its current list
     */
    slot = GEO_CTX_TMR_SLOT0;

    geo_ctx_tmr_stop(&tx_p->rpc_guard_timer);
    geo_ctx_tmr_start(slot,
            &tx_p->rpc_guard_timer,
            time_ms * 1000,
            geo_proc_timeout_CBK,
            (void*) tx_p);

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
int geo_proc_module_init(uint32_t context_count) 
{
    geo_proc_ctx_t *p;
    uint32_t idxCur;
    ruc_obj_desc_t *pnext;
    uint32_t ret = 0;


    geo_proc_context_allocated = 0;
    geo_proc_context_count = context_count;
    geo_proc_context_freeListHead = (geo_proc_ctx_t*) NULL;
    /*
    **  create the active list
    */
    ruc_listHdrInit((ruc_obj_desc_t*) & geo_proc_context_activeListHead);

    /*
     ** create the Transaction context pool
     */
    geo_proc_context_freeListHead = (geo_proc_ctx_t*) ruc_listCreate(context_count, sizeof (geo_proc_ctx_t));
    if (geo_proc_context_freeListHead == (geo_proc_ctx_t*) NULL) {
        /* 
        **  out of memory
        */
        severe("Out of memory: requested size %d",(int)(context_count * sizeof (geo_proc_ctx_t)));
        return -1;
    }
    /*
    ** store the pointer to the first context
    */
    geo_proc_context_pfirst = geo_proc_context_freeListHead;
    /*
    **  initialize each entry of the free list
    */
    idxCur = 0;
    pnext = (ruc_obj_desc_t*) NULL;
    while ((p = (geo_proc_ctx_t*) ruc_objGetNext((ruc_obj_desc_t*) geo_proc_context_freeListHead,
            &pnext))
            != (geo_proc_ctx_t*) NULL) {

        p->index = idxCur;
        p->free = TRUE;
        geo_proc_ctxInit(p, TRUE);
	p->record_buf_p = NULL;
        idxCur++;
    }

    /*
     ** Initialize the timer module:period is 100 ms
     */
    geo_ctx_tmr_init(100, 15);
    /*
    ** Clear the statistics counter
    */
    memset(geo_proc_stats, 0, sizeof (uint64_t) * GEO_CTX_COUNTER_MAX);
    geo_proc_debug_init();

    return ret;
}
/**
**____________________________________________________
*  Get the number of free context in the transaction context distributor

  @param none
  @retval <>NULL, success->pointer to the allocated context
  @retval NULL, error ->out of context
*/
int geo_proc_get_free_ctx_number(void){
  return (geo_proc_context_count-geo_proc_context_allocated);
}

