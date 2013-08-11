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
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/core/ruc_buffer_api.h>
#include "expgw_export.h"
#include "expgw_fid_cache.h"
#include <rozofs/rpc/epproto.h>

DECLARE_PROFILING(epp_profiler_t);


expgw_ctx_t *expgw_ctx_freeListHead;  /**< head of list of the free context  */
expgw_ctx_t expgw_ctx_activeListHead;  /**< list of the active context     */

uint32_t    expgw_ctx_count;           /**< Max number of contexts    */
uint32_t    expgw_ctx_allocated;      /**< current number of allocated context        */
expgw_ctx_t *expgw_ctx_pfirst;  /**< pointer to the first context of the pool */
uint64_t  expgw_global_object_index = 0;




uint64_t expgw_stats[EXPGW_COUNTER_MAX];


/**
* Buffers information
*/
int expgw_north_small_buf_count= 0;
int expgw_north_small_buf_sz= 0;
int expgw_north_large_buf_count= 0;
int expgw_north_large_buf_sz= 0;
int expgw_south_small_buf_count= 0;
int expgw_south_small_buf_sz= 0;
int expgw_south_large_buf_count= 0;
int expgw_south_large_buf_sz= 0;

void *expgw_pool[_EXPGW_MAX_POOL];

uint32_t expgw_seqnum = 1;


#define MICROLONG(time) ((unsigned long long)time.tv_sec * 1000000 + time.tv_usec)
#define EXPGW_DEBUG_TOPIC      "rozofsmount_res"
static char    myBuf[UMA_DBG_MAX_SEND_SIZE];

/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void expgw_debug_show(uint32_t tcpRef, void *bufRef) {
  char           *pChar=myBuf;

  pChar += sprintf(pChar,"number of transaction contexts (initial/allocated) : %u/%u\n",expgw_ctx_count,expgw_ctx_allocated);
  pChar += sprintf(pChar,"Statistics\n");
  pChar += sprintf(pChar,"SEND           : %10llu\n",(unsigned long long int)expgw_stats[EXPGW_SEND]);  
  pChar += sprintf(pChar,"SEND_ERR       : %10llu\n",(unsigned long long int)expgw_stats[EXPGW_SEND_ERROR]);  
  pChar += sprintf(pChar,"RECV_OK        : %10llu\n",(unsigned long long int)expgw_stats[EXPGW_RECV_OK]);  
  pChar += sprintf(pChar,"RECV_OUT_SEQ   : %10llu\n",(unsigned long long int)expgw_stats[EXPGW_RECV_OUT_SEQ]);  
  pChar += sprintf(pChar,"RTIMEOUT       : %10llu\n",(unsigned long long int)expgw_stats[EXPGW_TIMEOUT]);  
  pChar += sprintf(pChar,"\n");
  pChar += sprintf(pChar,"Buffer Pool (name[size] :initial/current\n");
  pChar += sprintf(pChar,"North interface Buffers            \n");  
  pChar += sprintf(pChar,"  small[%6d]  : %6d/%d\n",expgw_north_small_buf_sz,expgw_north_small_buf_count,
                                                         ruc_buf_getFreeBufferCount(EXPGW_NORTH_SMALL_POOL)); 
  pChar += sprintf(pChar,"  large[%6d]  : %6d/%d\n",expgw_north_large_buf_sz,expgw_north_large_buf_count, 
                                                         ruc_buf_getFreeBufferCount(EXPGW_NORTH_LARGE_POOL)); 
  pChar += sprintf(pChar,"South interface Buffers            \n");  
  pChar += sprintf(pChar,"  small[%6d]  : %6d/%d\n",expgw_south_small_buf_sz,expgw_south_small_buf_count, 
                                                         ruc_buf_getFreeBufferCount(EXPGW_SOUTH_SMALL_POOL)); 
  pChar += sprintf(pChar,"  large[%6d]  : %6d/%d\n",expgw_south_large_buf_sz,expgw_south_large_buf_count,
                                                         ruc_buf_getFreeBufferCount(EXPGW_SOUTH_LARGE_POOL)); 
  if (bufRef != NULL) uma_dbg_send(tcpRef,bufRef,TRUE,myBuf);
  else printf("%s",myBuf);

}
/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void expgw_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
  expgw_debug_show(tcpRef,bufRef);
}


/*__________________________________________________________________________
  Register to the debug SWBB
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void expgw_debug_init() {
  uma_dbg_addTopic(EXPGW_DEBUG_TOPIC, expgw_debug); 
}


/*
**  END OF DEBUG
*/



/*-----------------------------------------------
**   expgw_getObjCtx_p

** based on the object index, that function
** returns the pointer to the object context.
**
** That function may fails if the index is
** not a Transaction context index type.
**
@param     : MS index
@retval   : NULL if error

*/

expgw_ctx_t *expgw_getObjCtx_p(uint32_t object_index)
{
   uint32_t index;
   expgw_ctx_t *p;

   /*
   **  Get the pointer to the context
   */
   index = object_index & RUC_OBJ_MASK_OBJ_IDX; 
   if ( index >= expgw_ctx_count)
   {
      /*
      ** the MS index is out of range
      */
      severe( "expgw_getObjCtx_p(%d): index is out of range, index max is %d",index,expgw_ctx_count );   
     return (expgw_ctx_t*)NULL;
   }
   p = (expgw_ctx_t*)ruc_objGetRefFromIdx((ruc_obj_desc_t*)expgw_ctx_freeListHead,
                                       index);
   return ((expgw_ctx_t*)p);
}

/*-----------------------------------------------
**   expgw_getObjCtx_ref

** based on the object index, that function
** returns the pointer to the object context.
**
** That function may fails if the index is
** not a Transaction context index type.
**
@param     : MS index
@retval   :-1 out of range

*/

uint32_t expgw_getObjCtx_ref(expgw_ctx_t *p)
{
   uint32_t index;
   index = (uint32_t) ( p - expgw_ctx_pfirst);
   index = index/sizeof(expgw_ctx_t);

   if ( index >= expgw_ctx_count)
   {
      /*
      ** the MS index is out of range
      */
      severe( "expgw_getObjCtx_p(%d): index is out of range, index max is %d",index,expgw_ctx_count );   
     return (uint32_t) -1;
   }
;
   return index;
}




/*
**____________________________________________________
*/
/**
   expgw_init

  initialize the Transaction management module

@param     : NONE
@retval   none   :
*/
void expgw_init()
{   
   expgw_ctx_pfirst = (expgw_ctx_t*)NULL;

   expgw_ctx_allocated = 0;
   expgw_ctx_count = 0;
}

/*
**____________________________________________________
*/
/**
   expgw_ctxInit

  create the transaction context pool

@param     : pointer to the Transaction context
@retval   : none
*/
void  expgw_ctxInit(expgw_ctx_t *p,uint8_t creation)
{

  p->integrity  = -1;     /* the value of this field is incremented at 
					      each MS ctx allocation */
                          
  p->recv_buf     = NULL;
  p->socketRef    = -1;
  p->xmitBuf     = NULL;  
  p->opcode      = 0;
  p->src_transaction_id = 0;
  p->fid_cache_entry = NULL;
  p->profiler_probe = NULL;
  p->profiler_time  = 0;
 
}

/*
**__________________________________________________________________________
*/
/**
  allocate a  context to handle a client read/write transaction

  @param     : none
  @retval <>NULL pointer to the allocated context
  @retval NULL out of free context
*/
expgw_ctx_t *expgw_alloc_context()
{
   expgw_ctx_t *p;
   

   /*
   **  Get the first free context
   */
   if ((p =(expgw_ctx_t*)ruc_objGetFirst((ruc_obj_desc_t*)expgw_ctx_freeListHead))
           == (expgw_ctx_t*)NULL)
   {
     /*
     ** out of Transaction context descriptor try to free some MS
     ** context that are out of date 
     */
     severe( "not able to get a tx context" );
     return NULL;
   }
   /*
   **  reinitilisation of the context
   */
   expgw_ctxInit(p,FALSE);   
  /*
  ** init of the routing context
  */
  expgw_routing_ctx_init(&p->expgw_routing_ctx);

   /*
   ** remove it for the linked list
   */
   expgw_ctx_allocated++;
   p->free = FALSE;   
   ruc_objRemove((ruc_obj_desc_t*)p);
 
   return p;
}

/*
**__________________________________________________________________________
*/
/**
* release a read/write context that has been use for either a read or write operation

  @param : pointer to the context
  
  @retval <>NULL pointer to the allocated context
  @retval NULL out of free context
*/
void expgw_release_context(expgw_ctx_t *ctx_p)
{

  /*
  ** release the buffer that was carrying the initial request
  */
  if (ctx_p->recv_buf != NULL) 
  {
    ruc_buf_freeBuffer(ctx_p->recv_buf);
    ctx_p->recv_buf = NULL;
  }
  ctx_p->socketRef = -1;
  if (ctx_p->xmitBuf != NULL) 
  {
    ruc_buf_freeBuffer(ctx_p->xmitBuf);
    ctx_p->xmitBuf = NULL;
  }
  /*
  ** check if there is an xmit buffer to release since it might be the case
  ** when there were 2 available load balancing groups
  */
  expgw_routing_release_buffer(&ctx_p->expgw_routing_ctx);
  
  if (ctx_p->fid_cache_entry != NULL)
  {
     com_cache_entry_t *p;
     
     p = (com_cache_entry_t*)ctx_p->fid_cache_entry;
     expgw_fid_release_entry(p->usr_entry_p);
     ctx_p->fid_cache_entry = NULL;  
  }

  /*
  ** remove it from any other list and re-insert it on the free list
  */
  ruc_objRemove((ruc_obj_desc_t*) ctx_p);
   
   /*
   **  insert it in the free list
   */
   expgw_ctx_allocated--;
   ctx_p->free = TRUE;
   /*
   ** update the profiler
   */
   STOP_PROFILING_EXPGW(ctx_p);
   
   ruc_objInsertTail((ruc_obj_desc_t*)expgw_ctx_freeListHead,
                     (ruc_obj_desc_t*) ctx_p);
                     
}


/*
**__________________________________________________________________________
*/
/**
   expgw_module_init

  create the Transaction context pool


@retval   : RUC_OK : done
@retval          RUC_NOK : out of memory
*/
uint32_t expgw_module_init()
{
   expgw_ctx_t *p;
   uint32_t idxCur,xRefCur;
   ruc_obj_desc_t *pnext;
   uint32_t ret = RUC_OK;
   

    expgw_north_small_buf_count  = EXPGW_NORTH_MOD_INTERNAL_READ_BUF_CNT ;
    expgw_north_small_buf_sz     = EXPGW_NORTH_MOD_INTERNAL_READ_BUF_SZ    ;
    expgw_north_large_buf_count  = EXPGW_NORTH_MOD_XMIT_BUF_CNT ;
    expgw_north_large_buf_sz     = EXPGW_NORTH_MOD_XMIT_BUF_SZ    ;
    
    expgw_south_small_buf_count  = EXPGW_CNF_NO_BUF_CNT ;
    expgw_south_small_buf_sz     = EXPGW_CNF_NO_BUF_SZ  ;
    expgw_south_large_buf_count  = EXPGW_SOUTH_TX_XMIT_BUF_CNT   ;
    expgw_south_large_buf_sz     = EXPGW_SOUTH_TX_XMIT_BUF_SZ  ;  
   
   expgw_ctx_allocated = 0;
   expgw_ctx_count = EXPGW_CTX_CNT;
 
   expgw_ctx_freeListHead = (expgw_ctx_t*)NULL;

   /*
   **  create the active list
   */
   ruc_listHdrInit((ruc_obj_desc_t*)&expgw_ctx_activeListHead);    

   /*
   ** create the Read/write Transaction context pool
   */
   expgw_ctx_freeListHead = (expgw_ctx_t*)ruc_listCreate(expgw_ctx_count,sizeof(expgw_ctx_t));
   if (expgw_ctx_freeListHead == (expgw_ctx_t*)NULL)
   {
     /* 
     **  out of memory
     */

     RUC_WARNING(expgw_ctx_count*sizeof(expgw_ctx_t));
     return RUC_NOK;
   }
   /*
   ** store the pointer to the first context
   */
   expgw_ctx_pfirst = expgw_ctx_freeListHead;

   /*
   **  initialize each entry of the free list
   */
   idxCur = 0;
   xRefCur = 0;
   pnext = (ruc_obj_desc_t*)NULL;
   while ((p = (expgw_ctx_t*)ruc_objGetNext((ruc_obj_desc_t*)expgw_ctx_freeListHead,
                                        &pnext))
               !=(expgw_ctx_t*)NULL) 
   {
  
      p->index = idxCur;
      p->free  = TRUE;
      expgw_ctxInit(p,TRUE);
      idxCur++;
   } 

   /*
   ** Initialize the RESUME and SUSPEND timer module: 100 ms
   */
//   com_tx_tmr_init(100,15); 
   /*
   ** Clear the statistics counter
   */
   memset(expgw_stats,0,sizeof(uint64_t)*EXPGW_COUNTER_MAX);
   expgw_debug_init();
      
   
   while(1)
   {
      expgw_pool[_EXPGW_NORTH_SMALL_POOL]= ruc_buf_poolCreate(expgw_north_small_buf_count,expgw_north_small_buf_sz);
      if (expgw_pool[_EXPGW_NORTH_SMALL_POOL] == NULL)
      {
         ret = RUC_NOK;
         severe( "xmit ruc_buf_poolCreate(%d,%d)", expgw_north_small_buf_count, expgw_north_small_buf_sz ); 
         break;
      }
      ruc_buffer_debug_register_pool("NorthSmall",expgw_pool[_EXPGW_NORTH_SMALL_POOL]);
      expgw_pool[_EXPGW_NORTH_LARGE_POOL] = ruc_buf_poolCreate(expgw_north_large_buf_count,expgw_north_large_buf_sz);
      if (expgw_pool[_EXPGW_NORTH_LARGE_POOL] == NULL)
      {
         ret = RUC_NOK;
         severe( "rcv ruc_buf_poolCreate(%d,%d)", expgw_north_large_buf_count, expgw_north_large_buf_sz ); 
	 break;
      }
      ruc_buffer_debug_register_pool("NorthLarge",expgw_pool[_EXPGW_NORTH_LARGE_POOL]);
      expgw_pool[_EXPGW_SOUTH_SMALL_POOL]= ruc_buf_poolCreate(expgw_south_small_buf_count,expgw_south_small_buf_sz);
      if (expgw_pool[_EXPGW_SOUTH_SMALL_POOL] == NULL)
      {
         ret = RUC_NOK;
         severe( "xmit ruc_buf_poolCreate(%d,%d)", expgw_south_small_buf_count, expgw_south_small_buf_sz ); 
         break;
      }
      ruc_buffer_debug_register_pool("SouthSmall",expgw_pool[_EXPGW_SOUTH_SMALL_POOL]);
      expgw_pool[_EXPGW_SOUTH_LARGE_POOL] = ruc_buf_poolCreate(expgw_south_large_buf_count,expgw_south_large_buf_sz);
      if (expgw_pool[_EXPGW_SOUTH_LARGE_POOL] == NULL)
      {
         ret = RUC_NOK;
         severe( "rcv ruc_buf_poolCreate(%d,%d)", expgw_south_large_buf_count, expgw_south_large_buf_sz ); 
	 break;
      }
      ruc_buffer_debug_register_pool("SouthLarge",expgw_pool[_EXPGW_SOUTH_LARGE_POOL]);      
   break;
   }
   return ret;
}
