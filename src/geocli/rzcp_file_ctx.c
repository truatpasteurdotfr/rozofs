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
#include "rzcp_file_ctx.h"

rzcp_copy_ctx_t *rzcp_context_freeListHead; /**< head of list of the free context  */
rzcp_copy_ctx_t rzcp_context_activeListHead; /**< list of the active context     */

uint32_t rzcp_context_count; /**< Max number of contexts    */
int32_t rzcp_context_allocated; /**< current number of allocated context        */
rzcp_copy_ctx_t *rzcp_context_pfirst; /**< pointer to the first context of the pool */
uint32_t rzcp_global_context_id = 0;

uint64_t rzcp_stats[RZCP_CTX_COUNTER_MAX];
//uint32_t geo_srv_timestamp=1;
rzcp_profiler_t rzcp_profiler;
int rzcp_log_enable;

#define MICROLONG(time) ((unsigned long long)time.tv_sec * 1000000 + time.tv_usec)
#define RZCP_CTX_DEBUG_TOPIC      "rzcp_ctx"

/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void rzcp_debug_show(uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();

    pChar += sprintf(pChar, "number of rzcpy contexts (initial/allocated) : %u/%u\n", rzcp_context_count, rzcp_context_allocated);
    pChar += sprintf(pChar, "context size (bytes)                         : %u\n", (unsigned int) sizeof (rzcp_copy_ctx_t));
    pChar += sprintf(pChar, "Total memory size (bytes)                    : %u\n", (unsigned int) sizeof (rzcp_copy_ctx_t) * rzcp_context_count);
    pChar += sprintf(pChar, "Statistics\n");
    pChar += sprintf(pChar, "CTX_MISMATCH   : %10llu\n", (unsigned long long int) rzcp_stats[RZCP_CTX_CTX_MISMATCH]);
    pChar += sprintf(pChar, "NO_CTX_ERROR   : %10llu\n", (unsigned long long int) rzcp_stats[RZCP_CTX_NO_CTX_ERROR]);
    pChar += sprintf(pChar, "CPY_INIT_ERR   : %10llu\n", (unsigned long long int) rzcp_stats[RZCP_CTX_CPY_INIT_ERR]);
    pChar += sprintf(pChar, "BAD_READ_LEN   : %10llu\n", (unsigned long long int) rzcp_stats[RZCP_CTX_CPY_BAD_READ_LEN]);
    pChar += sprintf(pChar, "CPY_READ_ERR   : %10llu\n", (unsigned long long int) rzcp_stats[RZCP_CTX_CPY_READ_ERR]);
    pChar += sprintf(pChar, "CPY_WRITE_ERR  : %10llu\n", (unsigned long long int) rzcp_stats[RZCP_CTX_CPY_WRITE_ERR]);
    pChar += sprintf(pChar, "CPY_ABORT_ERR  : %10llu\n", (unsigned long long int) rzcp_stats[RZCP_CTX_CPY_ABORT_ERR]);
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
void rzcp_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
    rzcp_debug_show(tcpRef, bufRef);
}

/*__________________________________________________________________________
  Register to the debug SWBB
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void rzcp_debug_init() {
    uma_dbg_addTopic(RZCP_CTX_DEBUG_TOPIC, rzcp_debug);
}

/*
 **  END OF DEBUG
 */

/*
**____________________________________________________
*/
/**
    rzcp_getObjCtx_p

  based on the object index, that function
  returns the pointer to the object context.
 
  That function may fails if the index is
  not a Transaction context index type.
 
   @param     : object index
   @retval   : NULL if error
*/
rzcp_copy_ctx_t *rzcp_getObjCtx_p(uint32_t context_id) {
    uint32_t index;
    rzcp_copy_ctx_t *p;

    /*
     **  Get the pointer to the context
     */
    index = context_id & RUC_OBJ_MASK_OBJ_IDX;
    if (index >= rzcp_context_count) {
        /*
         ** the index is out of range
         */
        severe( "rzcp_getObjCtx_p(%d): index is out of range, index max is %d", index, rzcp_context_count );
        return (rzcp_copy_ctx_t*) NULL;
    }
    p = (rzcp_copy_ctx_t*) ruc_objGetRefFromIdx((ruc_obj_desc_t*) rzcp_context_freeListHead,
            index);
    return ((rzcp_copy_ctx_t*) p);
}
/*
**____________________________________________________
*/
/**
    rzcp_getObjCtx_ref

  based on the object index, that function
  returns the pointer to the object context.
 
  That function may fails if the index is
  not a Transaction context index type.
 
  @param     : pointer to the context
  @retval   : >= 0: index of the context
  @retval  <0 : error

*/
uint32_t rzcp_getObjCtx_ref(rzcp_copy_ctx_t *p) {
    uint32_t index;
    index = (uint32_t) (p - rzcp_context_pfirst);
    index -= 1;

    if (index >= rzcp_context_count) {
        /*
         ** the MS index is out of range
         */
        severe( "rzcp_getObjCtx_p(%d): index is out of range, index max is %d", index, rzcp_context_count );
	errno = ERANGE;
        return (uint32_t) - 1;
    }
    return index;
}

/*
**____________________________________________________
*/
/**
   rzcp_ctxInit

  create the  context pool

@param     : pointer to the context
@retval   : none
 */
void rzcp_ctxInit(rzcp_copy_ctx_t *p, uint8_t creation) {

    p->integrity = -1; /* the value of this field is incremented at 
					      each ctx allocation */

    memset(&p->read_ctx,0,sizeof(rzcp_file_ctx_t));
    memset(&p->write_ctx,0,sizeof(rzcp_file_ctx_t));
    p->buffer = NULL;
    int k;
    for (k = 0; k < SHAREMEM_PER_FSMOUNT; k++) p->shared_buf_ref[k] = NULL;
    p->rzcp_copy_cbk = NULL;
    p->rzcp_caller_cbk = NULL;
}
/*
**____________________________________________________
*/
/**
   rzcp_alloc

   create a  context
    That function tries to allocate a free  context. 
    In case of success, it returns the pointer to the context.
 
    @param     : none

    @retval   : <>NULL: pointer to the allocated context
    @retval    NULL if out of context.
*/
rzcp_copy_ctx_t *rzcp_alloc() {
    rzcp_copy_ctx_t *p;

    /*
     **  Get the first free context
     */
    if ((p = (rzcp_copy_ctx_t*) ruc_objGetFirst((ruc_obj_desc_t*) rzcp_context_freeListHead))
            == (rzcp_copy_ctx_t*) NULL) {
        /*
         ** out of Transaction context descriptor try to free some MS
         ** context that are out of date 
         */
        RZCP_CTX_STATS(RZCP_CTX_NO_CTX_ERROR);
        severe( "out of context" );
        return NULL;
    }
    /*
     **  reinitilisation of the context
     */
    rzcp_ctxInit(p, FALSE);
    /*
     ** remove it for the linked list
     */
    rzcp_context_allocated++;
    p->free = FALSE;

    ruc_objRemove((ruc_obj_desc_t*) p);

    return p;
}
/*
**____________________________________________________
*/
/**
   rzcp_createIndex

  create a  context given by index 
   That function tries to allocate a free context. 
   In case of success, it returns the index of the context.
 
@param     : context_id is the reference of the context
@retval   : MS controller reference (if OK)
retval     -1 if out of context.
 */
uint32_t rzcp_createIndex(uint32_t context_id) {
    rzcp_copy_ctx_t *p;

    /*
     **  Get the first free context
     */
    p = rzcp_getObjCtx_p(context_id);
    if (p == NULL) {
        severe( "object ref out of range: %u", context_id );
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
    rzcp_ctxInit(p, FALSE);
    /*
     ** remove it for the linked list
     */
    rzcp_context_allocated++;


    p->free = FALSE;
    ruc_objRemove((ruc_obj_desc_t*) p);

    return RUC_OK;
}
/*
**____________________________________________________
*/
/**
   delete a  context from idx
   

   @param     : index of the context
   
   @retval   : RUC_OK : context has been deleted
   @retval     RUC_NOK : out of limit index.
*/
uint32_t rzcp_free_from_idx(uint32_t context_id) {
    rzcp_copy_ctx_t *p;

    if (context_id >= rzcp_context_count) {
        /*
         ** index is out of limits
         */
        return RUC_NOK;
    }
    /*
    ** get the reference from idx
    */
    p = rzcp_getObjCtx_p(context_id);
    /*
     ** remove it from the active list
     */
    ruc_objRemove((ruc_obj_desc_t*) p);
    /*
    ** release the potentially allocated resources
    */
    int k;
    for (k = 0; k <SHAREMEM_PER_FSMOUNT; k++)
    {
      if (p->shared_buf_ref[k]!= NULL) 
      {
	uint32_t *p32 = (uint32_t*)ruc_buf_getPayload(p->shared_buf_ref[k]);    
	/*
	** clear the timestamp
	*/
	*p32 = 0;
	ruc_buf_freeBuffer(p->shared_buf_ref[k]);
	p->shared_buf_ref[k] = NULL;
      }
    }
    if (p->buffer != NULL)
    {
       free(p->buffer);
       p->buffer = NULL;    
    }
    /*
     **  insert it in the free list
     */
    rzcp_context_allocated--;
    if (rzcp_context_allocated < 0)
    {
      severe("double release");
    }
    p->free = TRUE;
    ruc_objInsertTail((ruc_obj_desc_t*) rzcp_context_freeListHead,
            (ruc_obj_desc_t*) p);

    return RUC_OK;
}
/*
**____________________________________________________
*/
/**
   delete a  context

   If the  context is out of limit, and  error is returned.

   @param     : pointer to the context
   @retval   : RUC_OK : context has been deleted
   @retval     RUC_NOK : out of limit index.

*/
uint32_t rzcp_free_from_ptr(rzcp_copy_ctx_t *p) {
    uint32_t context_id;

    context_id = rzcp_getObjCtx_ref(p);
    if (context_id == (uint32_t) - 1) {
        RZCP_CTX_STATS(RZCP_CTX_CTX_MISMATCH);
        return RUC_NOK;
    }
    return (rzcp_free_from_idx(context_id));
}


/*
**____________________________________________________
*/
/**
   rzcp_module_init

  create the rzcopy context pool

@param     : context_count : number of contexts
@retval   : 0 : done
@retval     -1 : out of memory
 */
int rzcp_module_init(uint32_t context_count) 
{
    rzcp_copy_ctx_t *p;
    uint32_t idxCur;
    ruc_obj_desc_t *pnext;
    uint32_t ret = 0;

    memset(&rzcp_profiler,0,sizeof(rzcp_profiler_t));
    rzcp_log_enable = 0;
    

    rzcp_context_allocated = 0;
    rzcp_context_count = context_count;
    rzcp_context_freeListHead = (rzcp_copy_ctx_t*) NULL;
    /*
    **  create the active list
    */
    ruc_listHdrInit((ruc_obj_desc_t*) & rzcp_context_activeListHead);

    /*
     ** create the Transaction context pool
     */
    rzcp_context_freeListHead = (rzcp_copy_ctx_t*) ruc_listCreate(context_count, sizeof (rzcp_copy_ctx_t));
    if (rzcp_context_freeListHead == (rzcp_copy_ctx_t*) NULL) {
        /* 
        **  out of memory
        */
        severe("Out of memory: requested size %d",(int)(context_count * sizeof (rzcp_copy_ctx_t)));
        return -1;
    }
    /*
    ** store the pointer to the first context
    */
    rzcp_context_pfirst = rzcp_context_freeListHead;
    /*
    **  initialize each entry of the free list
    */
    idxCur = 0;
    pnext = (ruc_obj_desc_t*) NULL;
    while ((p = (rzcp_copy_ctx_t*) ruc_objGetNext((ruc_obj_desc_t*) rzcp_context_freeListHead,
            &pnext))
            != (rzcp_copy_ctx_t*) NULL) {

        p->index = idxCur;
        p->free = TRUE;
        rzcp_ctxInit(p, TRUE);
        idxCur++;
    }
    /*
    ** Clear the statistics counter
    */
    memset(rzcp_stats, 0, sizeof (uint64_t) * RZCP_CTX_COUNTER_MAX);
    rzcp_debug_init();

    return ret;
}
/**
**____________________________________________________
*  Get the number of free context in the transaction context distributor

  @param none
  @retval <>NULL, success->pointer to the allocated context
  @retval NULL, error ->out of context
*/
int rzcp_get_free_ctx_number(void){
  return (rzcp_context_count-rzcp_context_allocated);
}

