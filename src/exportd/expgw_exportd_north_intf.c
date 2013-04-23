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
#include <stdlib.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <fcntl.h> 
#include <sys/un.h>             
#include <errno.h>  

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/core/ruc_buffer_api.h>
#include <rozofs/core/ruc_list.h>
#include <rozofs/core/af_unix_socket_generic_api.h>
#include <rozofs/core/af_unix_socket_generic.h>
#include "expgw_export.h"
#include <rozofs/core/rozofs_tx_api.h>
#include <rozofs/core/rozofs_socket_family.h>
#include <rozofs/core/af_inet_stream_api.h>

 /**
* Buffers information
*/
int expgw_exportd_read_write_buf_count= 0;   /**< number of buffer allocated for read/write on north interface */
int expgw_exportd_read_write_buf_sz= 0;      /**<read:write buffer size on north interface */

void *expgw_exportd_north_buffer_pool_p = NULL;  /**< reference of the read/write buffer pool */
 

/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   THis the callback that is activated upon the reception of an exportd
   request 

    
  @param socket_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
   @retval : TRUE-> xmit ready event expected
  @retval : FALSE-> xmit  ready event not expected
*/
void expgw_exportd_req_rcv_cbk(void *userRef,uint32_t  socket_ctx_idx, void *recv_buf)
{
 #warning fake expgw_exportd_req_rcv_cbk()   
    ruc_buf_freeBuffer(recv_buf);
    
}


/*
**____________________________________________________
*/
/**
*  
  Callback to allocate a buffer for receiving a rpc message (mainly a RPC response
 
 
 The service might reject the buffer allocation because the pool runs
 out of buffer or because there is no pool with a buffer that is large enough
 for receiving the message because of a out of range size.

 @param userRef : pointer to a user reference: not used here
 @param socket_context_ref: socket context reference
 @param len : length of the incoming message
 
 @retval <>NULL pointer to a receive buffer
 @retval == NULL no buffer 
*/
void * expgw_exportd_north_RcvAllocBufCallBack(void *userRef,uint32_t socket_context_ref,uint32_t len)
{

   /*
   ** check if a small or a large buffer must be allocated
   */
   if (len >  expgw_exportd_read_write_buf_sz)
   {   
     return NULL;   
   }
   return ruc_buf_getBuffer(expgw_exportd_north_buffer_pool_p);      
}

/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   receiver ready function: called from socket controller.
   The module is intended to return if the receiver is ready to receive a new message
   and FALSE otherwise
   
   The application is ready to received if the north read/write buffer pool is not empty


  @param socket_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)

  @retval : TRUE-> receiver ready
  @retval : FALSE-> receiver not ready
*/
uint32_t expgw_exportd_north_userRcvReadyCallBack(void * socket_ctx_p,int socketId)
{

    uint32_t free_count = expgw_get_free_transaction_context();
    
    if (free_count < EXPGW_CTX_MIN_CNT)
    {
      return FALSE;
    }
    return TRUE;


}


/*__________________________________________________________________________
*/
/**
* test function that is called upon a failure on sending

 The application might use that callback if it has some other
 destination that can be used in case of failure of the current one
 If the application has no other destination to select, it is up to the
 application to release the buffer.
 

 @param userRef : pointer to a user reference: not used here
 @param socket_context_ref: socket context reference
 @param bufRef : pointer to the packet buffer on which the error has been encountered
 @param err_no : errno has reported by the sendto().
 
 @retval none
*/
void  expgw_exportd_north_userDiscCallBack(void *userRef,uint32_t socket_context_ref,void *bufRef,int err_no)
{

    /*
    ** release the current buffer if significant
    */
    if (bufRef != NULL) ruc_buf_freeBuffer(bufRef);
    /*
    ** release the context now and clean up all the attached buffer
    */
    af_unix_delete_socket(socket_context_ref);   
}



 /**
 *  socket configuration for the family
 */
 af_unix_socket_conf_t  af_inet_exportd_north_conf =
{
  1,  //           family: identifier of the socket family    */
  0,         /**< instance number within the family   */
  sizeof(uint32_t),  /* headerSize  -> size of the header to read                 */
  0,                 /* msgLenOffset->  offset where the message length fits      */
  sizeof(uint32_t),  /* msgLenSize  -> size of the message length field in bytes  */

  (1024*256), //        bufSize;         /* length of buffer (xmit and received)        */
  (300*1024), //        so_sendbufsize;  /* length of buffer (xmit and received)        */
  expgw_exportd_north_RcvAllocBufCallBack,  //    userRcvAllocBufCallBack; /* user callback for buffer allocation */
  expgw_exportd_req_rcv_cbk,           //    userRcvCallBack;   /* callback provided by the connection owner block */
  expgw_exportd_north_userDiscCallBack,   //    userDiscCallBack; /* callBack for TCP disconnection detection         */
  NULL,   //userConnectCallBack;     /**< callback for client connection only         */
  NULL,  //    userXmitDoneCallBack; /**< optional call that must be set when the application when to be warned when packet has been sent */
  expgw_exportd_north_userRcvReadyCallBack,  //    userRcvReadyCallBack; /* NULL for default callback                     */
  NULL,  //    userXmitReadyCallBack; /* NULL for default callback                    */
  NULL,  //    userXmitEventCallBack; /* NULL for default callback                    */
  rozofs_tx_get_rpc_msg_len_cbk,        /* userHdrAnalyzerCallBack ->NULL by default, function that analyse the received header that returns the payload  length  */
  ROZOFS_RPC_SRV,       /* recv_srv_type ---> service type for reception : ROZOFS_RPC_SRV or ROZOFS_GENERIC_SRV  */
  0,       /*   rpc_recv_max_sz ----> max rpc reception buffer size : required for ROZOFS_RPC_SRV only */

  NULL,  //    *userRef;             /* user reference that must be recalled in the callbacks */
  NULL,  //    *xmitPool; /* user pool reference or -1 */
  NULL   //    *recvPool; /* user pool reference or -1 */
}; 

/*
**____________________________________________________
*/
/**
   

  Creation of the north interface for rozofsmount (AF_INET)

@param     : src_ipaddr_host : source IP address in host format
@param     : src_port_host : port in host format
@param     : read_write_buf_count : number of read/write buffer
@param     : read_write_buf_sz : size of a read/write buffer

@retval   : RUC_OK : done
@retval          RUC_NOK : out of memory
*/

int expgw_exportd_north_interface_init(uint32_t src_ipaddr_host,uint16_t src_port_host,
                             int read_write_buf_count,int read_write_buf_sz)
{
   int ret = 0;
   

    expgw_exportd_read_write_buf_count  = read_write_buf_count;
    expgw_exportd_read_write_buf_sz     = read_write_buf_sz    ;
    while(1)
    {
      /*
      ** create the pool for receiving requests from rozofsmount
      */
      expgw_exportd_north_buffer_pool_p = ruc_buf_poolCreate(expgw_exportd_read_write_buf_count,
                                                            expgw_exportd_read_write_buf_sz);
      if (expgw_exportd_north_buffer_pool_p == NULL)
      {
         ret = -1;
         severe( "ruc_buf_poolCreate(%d,%d)", expgw_exportd_read_write_buf_count, expgw_exportd_read_write_buf_sz ); 
         break;
      }
      /*
      ** create the listening af unix socket on the north interface
      */
      af_inet_exportd_north_conf.rpc_recv_max_sz = expgw_exportd_read_write_buf_sz;
      ret =  af_inet_sock_listening_create("EXPGW_EXPORTD_NORTH",
                                            src_ipaddr_host,src_port_host, 
                                            &af_inet_exportd_north_conf   
                                            );

      break; 
    
    }
    return ret;

}


