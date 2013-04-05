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
#include "ppu_trace.h"
#include "uma_dbg_api.h"
#include "af_unix_socket_generic.h"

af_unix_ctx_generic_t *af_unix_context_freeListHead;  /**< head of list of the free context  */
af_unix_ctx_generic_t af_unix_context_activeListHead;  /**< list of the active context     */

void *af_unix_buffer_pool_tb[2];  /**< xmit and receive buffer pool */

/**
* Buffers information
*/
int af_unix_xmit_buf_count= 0;
int af_unix_xmit_buf_size= 0;
int af_unix_recv_buf_count= 0;
int af_unix_recv_buf_size= 0;

uint32_t    af_unix_context_count;           /**< Max number of contexts    */
uint32_t    af_unix_context_allocated;      /**< current number of allocated context        */
af_unix_ctx_generic_t *af_unix_context_pfirst;  /**< pointer to the first context of the pool */




#define MICROLONG(time) ((unsigned long long)time.tv_sec * 1000000 + time.tv_usec)
#define af_unix_DEBUG_TOPIC      "af_unix"
static char    myBuf[UMA_DBG_MAX_SEND_SIZE*4];
/*__________________________________________________________________________
*/
/**
*  that function checks if the stats of a context are empty

@param stats_p : poiinter to the stats context

@retval 0 non empty
@retval 1 empty
*/
int af_unix_check_empty_stats(rozofs_socket_stats_t *stats_p)
{
   if (stats_p->totalUpDownTransition!= 0) return 0;         /**< total number of bytes that has been sent */
   if (stats_p->totalXmitBytes!= 0) return 0;         /**< total number of bytes that has been sent */
   if (stats_p->totalXmitAttempts!= 0) return 0;  /**< total number of messages submitted       */
   if (stats_p->totalXmitSuccess!= 0) return 0;   /**< total number of messages submitted with success  */
   if (stats_p->totalXmitCongested!= 0) return 0; /**< total number of messages submitted for with EWOULDBLOCK is returned  */
   if (stats_p->totalXmitError!= 0) return 0;     /**< total number of messages submitted with an error  */

   /*
   ** xmit side
   */
   if (stats_p->totalRecvBytes!= 0) return 0;         /**< total number of bytes that has been sent */
   if (stats_p->totalRecv!= 0) return 0;  /**< total number of messages submitted       */
   if (stats_p->totalRecvSuccess!= 0) return 0;   /**< total number of messages submitted with success  */
   if (stats_p->totalRecvBadHeader!= 0) return 0;     /**< total number of messages submitted with an error  */
   if (stats_p->totalRecvBadLength!= 0) return 0;     /**< total number of messages submitted with an error  */
   if (stats_p->totalRecvOutoFBuf!= 0) return 0;     /**< total number of messages submitted with an error  */
   if (stats_p->totalRecvError!= 0) return 0;     /**< total number of messages submitted with an error  */
   return 1;
}

/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS:
  -
  RETURN: none
  ==========================================================================*/
void af_unix_debug_show(uint32_t tcpRef, void *bufRef) {
  char           *pChar=myBuf;
  pChar += sprintf(pChar,"number of AF_UNIX contexts [size](initial/allocated) :[%u] %u/%u\n",(unsigned int)sizeof(af_unix_ctx_generic_t),(unsigned int)af_unix_context_count,
                                                                                            (unsigned int)af_unix_context_allocated);
  pChar += sprintf(pChar,"Buffer Pool (name[size] :initial/current\n");
  pChar += sprintf(pChar,"Xmit Buffer ");
  pChar += sprintf(pChar,":[%6d] %6d/%d\n",af_unix_xmit_buf_size,af_unix_xmit_buf_count,
                                                         ruc_buf_getFreeBufferCount(af_unix_buffer_pool_tb[0]));
  pChar += sprintf(pChar,"Recv Buffer ");
  pChar += sprintf(pChar,":[%6d] %6d/%d\n",af_unix_recv_buf_size,af_unix_recv_buf_count,
                                                         ruc_buf_getFreeBufferCount(af_unix_buffer_pool_tb[1]));
{
    af_unix_ctx_generic_t *sock_p;
    ruc_obj_desc_t        *pnext;

    pnext = (ruc_obj_desc_t*)NULL;
    while ((sock_p = (af_unix_ctx_generic_t*)ruc_objGetNext((ruc_obj_desc_t*)&af_unix_context_activeListHead,
                                             &pnext))
               !=(af_unix_ctx_generic_t*)NULL)
    {
      rozofs_socket_stats_t *stats_p = &sock_p->stats;
      if (af_unix_check_empty_stats(stats_p) == 1) continue;

      pChar += sprintf(pChar,"\n--> %s\n",sock_p->nickname);
      pChar += sprintf(pChar,"   family/instance[sock_id]: %d/%d[%d]\n",sock_p->family,sock_p->instance_id,sock_p->socketRef);
      if (sock_p->af_family == AF_UNIX)
      {
        if (sock_p->remote_sun_path[0] != 0)
        {
          pChar += sprintf(pChar,"   sunpath(dst): %s\n",sock_p->remote_sun_path);
        }
        if (sock_p->src_sun_path[0] != 0)
        {
          pChar += sprintf(pChar,"   sunpath(src): %s\n",sock_p->src_sun_path);
        }
      }
      else
      {
        /*
        ** AF_INET case
        */
        struct  sockaddr_in vSckAddr;
        int               vSckAddrLen=sizeof(struct sockaddr);
        uint32_t          ipAddr;
        uint16_t          port;


        if((getpeername(sock_p->socketRef, (struct sockaddr *)&vSckAddr,(socklen_t*) &vSckAddrLen)) == -1)
        {
         pChar += sprintf(pChar,"   IP/port(dst):???.???.???.???:????\n");
        }
        else
        {
          ipAddr = (uint32_t) ntohl((uint32_t)(/*(struct sockaddr *)*/vSckAddr.sin_addr.s_addr));
          port   = ntohs((uint16_t)(vSckAddr.sin_port));
//          sock_p->remote_ipaddr_host = ipAddr;
//          sock_p->remote_port_host    = port;
         pChar += sprintf(pChar,"   IP/port(dst):%u.%u.%u.%u:%u\n", (ipAddr>>24)&0xFF, (ipAddr>>16)&0xFF,(ipAddr>>8)&0xFF,(ipAddr)&0xFF,port);
        }
        if((getsockname(sock_p->socketRef, (struct sockaddr *)&vSckAddr,(socklen_t*) &vSckAddrLen)) == -1)
        {
         pChar += sprintf(pChar,"   IP/port(src):???.???.???.???:????\n");
        }
        else
        {
          ipAddr = (uint32_t) ntohl((uint32_t)(/*(struct sockaddr *)*/vSckAddr.sin_addr.s_addr));
          port   = ntohs((uint16_t)(vSckAddr.sin_port));
//          sock_p->remote_ipaddr_host = ipAddr;
//          sock_p->remote_port_host    = port;
         pChar += sprintf(pChar,"   IP/port(src):%u.%u.%u.%u:%u\n", (ipAddr>>24)&0xFF, (ipAddr>>16)&0xFF,(ipAddr>>8)&0xFF,(ipAddr)&0xFF,port);
        }
      }
      pChar += sprintf(pChar,"   transmitter state     : %d /%d\n",sock_p->xmit.state,sock_p->xmit.xmit_credit);
      pChar += sprintf(pChar,"   Up/Down Transitions   : %llu\n",(unsigned long long int) stats_p->totalUpDownTransition);
      pChar += sprintf(pChar,"   Xmit Queue            : %s\n",ruc_objIsEmptyList((ruc_obj_desc_t*)&sock_p->xmit.xmitList[0])?"EMPTY":"NON EMPTY");
      pChar += sprintf(pChar,"   xmit/Recv Statistics:\n");
      pChar += sprintf(pChar,"    totalXmitBytes     : %16llu\n",(unsigned long long int) stats_p->totalXmitBytes);
      pChar += sprintf(pChar,"    totalXmitAttempts  : %16llu\n",(unsigned long long int) stats_p->totalXmitAttempts);
      pChar += sprintf(pChar,"    totalXmitSuccess   : %16llu\n",(unsigned long long int) stats_p->totalXmitSuccess);
      pChar += sprintf(pChar,"    totalXmitCongested : %16llu\n",(unsigned long long int) stats_p->totalXmitCongested);
      pChar += sprintf(pChar,"    totalXmitError     : %16llu\n",(unsigned long long int) stats_p->totalXmitError);

      /*
      ** xmit side
      */
      pChar += sprintf(pChar,"    totalRecvBytes     : %16llu\n",(unsigned long long int) stats_p->totalRecvBytes);
      pChar += sprintf(pChar,"    totalRecv          : %16llu\n",(unsigned long long int) stats_p->totalRecv);
      pChar += sprintf(pChar,"    totalRecvSuccess   : %16llu\n",(unsigned long long int) stats_p->totalRecvSuccess);
      pChar += sprintf(pChar,"    totalRecvBadHeader : %16llu\n",(unsigned long long int) stats_p->totalRecvBadHeader);
      pChar += sprintf(pChar,"    totalRecvBadLength : %16llu\n",(unsigned long long int) stats_p->totalRecvBadLength);
      pChar += sprintf(pChar,"    totalRecvOutoFBuf  : %16llu\n",(unsigned long long int) stats_p->totalRecvOutoFBuf);
      pChar += sprintf(pChar,"    totalRecvError     : %16llu\n",(unsigned long long int) stats_p->totalRecvError);



    }
}
  uma_dbg_send(tcpRef,bufRef,TRUE,myBuf);

}
/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS:
  -
  RETURN: none
  ==========================================================================*/
void af_unix_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
  af_unix_debug_show(tcpRef,bufRef);
}


/*__________________________________________________________________________
  Register to the debug SWBB
  ==========================================================================
  PARAMETERS:
  -
  RETURN: none
  ==========================================================================*/
void af_unix_debug_init() {
  uma_dbg_addTopic(af_unix_DEBUG_TOPIC, af_unix_debug);
}




/*
**  END OF DEBUG
*/


/*
**_____________________________________________
*/
/**
 based on the object index, that function
 returns the pointer to the object context.

 That function may fails if the index is
 not a Transaction context index type.
**
@param     : af_unix socket context index
@retval   : NULL if error
*/
af_unix_ctx_generic_t *af_unix_getObjCtx_p(uint32_t af_unix_ctx_id)
{
   uint32_t index;
   af_unix_ctx_generic_t *p;

   /*
   **  Get the pointer to the context
   */
   index = af_unix_ctx_id & RUC_OBJ_MASK_OBJ_IDX;
   if ( index >= af_unix_context_count)
   {
      /*
      ** the MS index is out of range
      */
      ERRLOG "af_unix_getObjCtx_p(%d): index is out of range, index max is %d",index,af_unix_context_count ENDERRLOG
     return (af_unix_ctx_generic_t*)NULL;
   }
   p = (af_unix_ctx_generic_t*)ruc_objGetRefFromIdx((ruc_obj_desc_t*)af_unix_context_freeListHead,
                                       index);
   return ((af_unix_ctx_generic_t*)p);
}

/*
**_____________________________________________
*/
/**
 based on the object index, that function
 returns the pointer to the object context.

 That function may fails if the index is
 not a Transaction context index type.

@param     : af_unix socket context index
@retval   :-1 out of range
*/

uint32_t af_unix_getObjCtx_ref(af_unix_ctx_generic_t *p)
{
   uint32_t index;
   index = (uint32_t) ( p - af_unix_context_pfirst);
 //  index = index/sizeof(af_unix_ctx_generic_t);
   index-=1;

   if ( index >= af_unix_context_count)
   {
      /*
      ** the MS index is out of range
      */
      ERRLOG "af_unix_getObjCtx_p(%d): index is out of range, index max is %d",index,af_unix_context_count ENDERRLOG
     return (uint32_t) -1;
   }
;
   return index;
}




/*
**____________________________________________________
*/
/**
   af_unix_init

  initialize the Transaction management module

@param     : NONE
@retval   none   :
*/
void af_unix_init()
{
   af_unix_context_pfirst = (af_unix_ctx_generic_t*)NULL;

   af_unix_context_allocated = 0;
   af_unix_context_count = 0;
}

/*
**____________________________________________________
*/
/**
   af_unix_ctxInit

  create the transaction context pool

@param     : pointer to the Transaction context
@retval   : none
*/
void  af_unix_ctxInit(af_unix_ctx_generic_t *p,uint8_t creation)
{
  int i;
  com_xmit_template_t   *xmit_p;
  com_recv_template_t   *recv_p;
  rozofs_socket_stats_t *stats_p;

  p->family       = -1;        /**< identifier of the socket family    */
  p->instance_id  = -1;        /**< instance number within the family   */
  p->nickname[0] = 0;

  p->af_family = -1;
  p->src_sun_path[0]    = 0;
  p->remote_sun_path[0] = 0;
  p->src_ipaddr_host    = -1;
  p->src_port_host      = -1;
  p->remote_ipaddr_host = -1;
  p->remote_port_host   = -1;

  p->socketRef       = -1;   /* clear the reference of the socket  */
  p->connectionId    = NULL;
  p->userRcvCallBack = NULL;
  p->userRcvAllocBufCallBack = NULL;
  p->userDiscCallBack = NULL;      /* callBack for TCP disconnection detection         */
  p->userRcvReadyCallBack  = NULL;  /* callBack for receiver ready         */
  p->userXmitReadyCallBack = NULL;
  p->userXmitEventCallBack = NULL;
  p->userConnectCallBack   = NULL;
  p->userXmitDoneCallBack   = NULL;
  p->userHdrAnalyzerCallBack   = NULL;
  p->userRef = NULL;               /* user reference that must be recalled in the callbacks */
  p->conf_p  = NULL;
  /*
  ** Transmitter init
  */
  xmit_p = &p->xmit;
  xmit_p->xmitPoolOrigin = af_unix_buffer_pool_tb[0];     /**<current pool reference                */
  xmit_p->xmitPoolRef = xmit_p->xmitPoolOrigin;        /**< head of the current xmit buffer pool */
  xmit_p->state = XMIT_IDLE;        /**< xmit fsm state                       */

  xmit_p->eoc_flag       = 0;    /**< fin de congestion flag               */
  xmit_p->congested_flag = 0;    /**< congested:1                          */
  xmit_p->xmit_req_flag  = 0;    /**< assert to 1 when xmit ready is required */
  xmit_p->filler_flag    = 0;    /**< congested:1                          */

  xmit_p->nbWrite    = 0;
  xmit_p->nb2Write   = 0;
  xmit_p->bufRefCurrent = NULL;      /**< reference of the current buffer to send or NULL if no buffer  */
  xmit_p->eoc_threshold = 0;      /**< current EOC threshold                */
  xmit_p->eoc_threshold_conf = AF_UNIX_CONGESTION_DEFAULT_THRESHOLD ;      /**< configured EOC threshold        */
  xmit_p->xmit_credit   = 0;;         /**< current xmit credit                 */
  xmit_p->xmit_credit_conf = AF_UNIX_XMIT_CREDIT_DEFAULT;    /**< configured xmit credit              */
  for (i = 0; i < UMA_MAX_TCP_XMIT_PRIO; i++)
  {
     ruc_listHdrInit((ruc_obj_desc_t *)&xmit_p->xmitList[i]);

  }
  /*
  ** receiver init
  */

  recv_p = &p->recv;
  recv_p->rcvPoolOrigin = af_unix_buffer_pool_tb[1];     /*current pool reference */
  recv_p->rcvPoolRef    = recv_p->rcvPoolOrigin;        /* it could be either the reference of
                                    ** the user bufferv reference pool or
				                    ** the default one used by the TCP
				                    ** connection*/

  recv_p->nbread    = 0;
  recv_p->nb2read   = 0;
  recv_p->bufRefCurrent = NULL;
  recv_p->state = RECV_IDLE;
  /*
  ** clear the rpc part of the stream receiver
  */
  memset(&recv_p->rpc,0,sizeof(com_rpc_recv_template_t));
  /*
  **  configuration parameters
  */
  recv_p->recv_credit_conf = AF_UNIX_RECV_CREDIT_DEFAULT;
  recv_p->headerSize   = 0;       /* size of the header to read                 */
  recv_p->msgLenOffset = 0;     /* offset where the message length fits       */
  recv_p->msgLenSize   = 0;       /* size of the message length field in bytes  */
  recv_p->bufSize      = 0;         /* length of buffer (xmit and received)        */
  /*
  ** Stats initialisation
  */
  stats_p = &p->stats;
  memset(stats_p,0,sizeof(rozofs_socket_stats_t));

}


/*-----------------------------------------------
**   af_unix_alloc

**  create a Transaction context
**   That function tries to allocate a free PDP
**   context. In case of success, it returns the
**   index of the Transaction context.
**
@param     : recli index
@param       relayCref : RELAY-C ref of the context
@retval   : MS controller reference (if OK)
@retval    NULL if out of context.
*/
af_unix_ctx_generic_t *af_unix_alloc()
{
   af_unix_ctx_generic_t *p;

   /*
   **  Get the first free context
   */
   if ((p =(af_unix_ctx_generic_t*)ruc_objGetFirst((ruc_obj_desc_t*)af_unix_context_freeListHead))
           == (af_unix_ctx_generic_t*)NULL)
   {
     /*
     ** out of Transaction context descriptor try to free some MS
     ** context that are out of date
     */
     ERRLOG "NOT ABLE TO GET an AF_UNIX CONTEXT" ENDERRLOG;
     return NULL;
   }
   /*
   **  reinitilisation of the context
   */
   af_unix_ctxInit(p,FALSE);
   /*
   ** remove it for the linked list
   */
   af_unix_context_allocated++;
   p->free = FALSE;


   ruc_objRemove((ruc_obj_desc_t*)p);
   /*
   ** insert in the active list the new element created
   */
   ruc_objInsertTail((ruc_obj_desc_t*)&af_unix_context_activeListHead,(ruc_obj_desc_t*)p);
   return p;
}
/*
**____________________________________________________
*/
/**
   af_unix_createIndex

  create a AF UNIX context given by index
   That function tries to allocate a free PDP
   context. In case of success, it returns the
   index of the Transaction context.

@param     : af_unix_ctx_id is the reference of the context
@retval   : MS controller reference (if OK)
retval     -1 if out of context.
*/
uint32_t af_unix_createIndex(uint32_t af_unix_ctx_id)
{
   af_unix_ctx_generic_t *p;

   /*
   **  Get the first free context
   */
   p = af_unix_getObjCtx_p(af_unix_ctx_id);
   if (p == NULL) {
    ERRLOG "MS ref out of range: %u", af_unix_ctx_id ENDERRLOG;
    return RUC_NOK;
  }
   /*
   ** return an error if the context is not free
   */
   if (p->free == FALSE) {
     ERRLOG "the context is not free : %u", af_unix_ctx_id ENDERRLOG;
     return RUC_NOK;
   }
   /*
   **  reinitilisation of the context
   */
   af_unix_ctxInit(p,FALSE);
   /*
   ** remove it for the linked list
   */
   af_unix_context_allocated++;


   p->free = FALSE;
   ruc_objRemove((ruc_obj_desc_t*)p);

   return RUC_OK;
}


/*
**____________________________________________________
*/
/**
   delete a AF_UNIX context

   That function is intended to be called when
   a Transaction context is deleted. It returns the
   Transaction context to the free list. The delete
   procedure of the MS automaton and
   controller are called by that service.

   If the Transaction context is out of limit, and
   error is returned.

@param     : MS Index
@retval   : RUC_OK : context has been deleted
@retval     RUC_NOK : out of limit index.
*/
uint32_t af_unix_free_from_idx(uint32_t af_unix_ctx_id)
{
   af_unix_ctx_generic_t *p ;

   if (af_unix_ctx_id >= af_unix_context_count )
   {
     /*
     ** index is out of limits
     */
     return RUC_NOK;
   }
   /*
   ** get the reference from idx
   */
   p = af_unix_getObjCtx_p(af_unix_ctx_id);
   /*
   **  remove the xmit block
   */
//   ruc_objRemove((ruc_obj_desc_t *)&p->xmitCtx);

   /*
   ** remove it from the active list
   */
   ruc_objRemove((ruc_obj_desc_t*) p);

   /*
   **  insert it in the free list
   */
   af_unix_context_allocated--;


   p->free = TRUE;
   ruc_objInsertTail((ruc_obj_desc_t*)af_unix_context_freeListHead,
                     (ruc_obj_desc_t*) p);

   return RUC_OK;

}
/*
**____________________________________________________
*/
/**
   af_unix_free_from_ptr

   delete a Transaction context
   That function is intended to be called when
   a Transaction context is deleted. It returns the
   Transaction context to the free list. The delete
   procedure of the MS automaton and
   controller are called by that service.

   If the Transaction context is out of limit, and
   error is returned.

@param     : pointer to the transaction context
@retval   : RUC_OK : context has been deleted
@retval     RUC_NOK : out of limit index.

*/
uint32_t af_unix_free_from_ptr(af_unix_ctx_generic_t *p)
{
  uint32_t af_unix_ctx_id;

  af_unix_ctx_id = af_unix_getObjCtx_ref(p);
  if (af_unix_ctx_id == (uint32_t) -1)
  {
    return RUC_NOK;
  }
  return (af_unix_free_from_idx(af_unix_ctx_id));

}
/*
**____________________________________________________
*/
/**
* allocate a xmit_buffer from the default AF_UNIX pool
*
 @param none

 @retval <> NULL address of the xmit buffer
 @retval == NULL out of buffer
*/
void *af_unix_alloc_xmit_buf()
{
  return ruc_buf_getBuffer(af_unix_buffer_pool_tb[0]);

}

/*
**____________________________________________________
*/
/**
* allocate a receive_buffer from the default AF_UNIX pool
*
 @param none

 @retval <> NULL address of the xmit buffer
 @retval == NULL out of buffer
*/
void *af_unix_alloc_recv_buf()
{
  return ruc_buf_getBuffer(af_unix_buffer_pool_tb[1]);

}


/*
**____________________________________________________
*/
/**
   af_unix_module_init

  create the Transaction context pool

@param     : af_unix_ctx_count  : number of Transaction context
@param     : max_xmit_buf_count : number of xmit buffers
@param     : max_xmit_buf_size  : xmit buffer size
@param     : max_recv_buf_count : number of receive buffers
@param     : max_recv_buf_count : receive buffer size

@retval   : RUC_OK : done
@retval          RUC_NOK : out of memory
*/
uint32_t af_unix_module_init(uint32_t af_unix_ctx_count,
                           int    max_xmit_buf_count,int max_xmit_buf_size,
                           int    max_recv_buf_count,int max_recv_buf_size
                            )
{
   af_unix_ctx_generic_t *p;
   uint32_t idxCur,xRefCur;
   ruc_obj_desc_t *pnext;
   uint32_t ret = RUC_OK;

    af_unix_xmit_buf_count  = max_xmit_buf_count ;
    af_unix_xmit_buf_size   = max_xmit_buf_size    ;
    af_unix_recv_buf_count  = max_recv_buf_count   ;
    af_unix_recv_buf_size   = max_recv_buf_size    ;



   af_unix_context_allocated = 0;
   af_unix_context_count = af_unix_ctx_count;

   while(1)
   {
      af_unix_buffer_pool_tb[0]= ruc_buf_poolCreate(af_unix_xmit_buf_count,af_unix_xmit_buf_size);
      if (af_unix_buffer_pool_tb[0] == NULL)
      {
         ret = RUC_NOK;
         ERRLOG "xmit ruc_buf_poolCreate(%d,%d)", af_unix_xmit_buf_count, af_unix_xmit_buf_size ENDERRLOG
         break;
      }
      af_unix_buffer_pool_tb[1] = ruc_buf_poolCreate(af_unix_recv_buf_count,af_unix_recv_buf_size);
      if (af_unix_buffer_pool_tb[1] == NULL)
      {
         ret = RUC_NOK;
         ERRLOG "rcv ruc_buf_poolCreate(%d,%d)", af_unix_recv_buf_count, af_unix_recv_buf_size ENDERRLOG
	 break;
     }

     af_unix_context_freeListHead = (af_unix_ctx_generic_t*)NULL;

     /*
     **  create the active list
     */
     ruc_listHdrInit((ruc_obj_desc_t*)&af_unix_context_activeListHead);

     /*
     ** create the af unix context pool
     */
     af_unix_context_freeListHead = (af_unix_ctx_generic_t*)ruc_listCreate(af_unix_ctx_count,sizeof(af_unix_ctx_generic_t));
     if (af_unix_context_freeListHead == (af_unix_ctx_generic_t*)NULL)
     {
       /*
       **  out of memory
       */

       RUC_WARNING(af_unix_ctx_count*sizeof(af_unix_ctx_generic_t));
       return RUC_NOK;
     }
     /*
     ** store the pointer to the first context
     */
     af_unix_context_pfirst = af_unix_context_freeListHead;

     /*
     **  initialize each entry of the free list
     */
     idxCur = 0;
     xRefCur = 0;
     pnext = (ruc_obj_desc_t*)NULL;
     while ((p = (af_unix_ctx_generic_t*)ruc_objGetNext((ruc_obj_desc_t*)af_unix_context_freeListHead,
                                          &pnext))
                 !=(af_unix_ctx_generic_t*)NULL)
     {

        p->index = idxCur;
        p->free  = TRUE;
        af_unix_ctxInit(p,TRUE);
        idxCur++;
     }

     af_unix_debug_init();
     break;
   }

   return ret;
}
