/*
 Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
 This file is part of Rozofs.

 Rozofs is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation; either version 3 of the License,
 or (at your option) any later version.

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
#include <rozofs/rozofs.h>
#include "config.h"
#include <rozofs/common/log.h>
#include <rozofs/core/af_unix_socket_generic_api.h>
#include <rozofs/core/rozofs_rpc_non_blocking_generic_srv.h>
#include <rozofs/core/ruc_buffer_debug.h>
#include <rozofs/common/profile.h>
#include "storio_disk_thread_intf.h"

DECLARE_PROFILING(spp_profiler_t); 
 
static int transactionId = 1; 
int        af_unix_disk_south_socket_ref = -1;
char       destination_socketName[128];
int        af_unix_disk_thread_count=0;

void   af_unix_disk_response_callback(void *userRef,uint32_t  socket_ctx_idx, void *recv_buf);
void * af_unix_disk_userRcvAllocBufCallBack(void *userRef,uint32_t socket_context_ref,uint32_t len);
void   af_unix_disk_disconnection_callback(void *userRef,uint32_t socket_context_ref,void *bufRef,int err_no);
 
int storio_disk_thread_create(char * hostname, int nb_threads) ;
 
 /**
 *  socket configuration for the family
 */
 af_unix_socket_conf_t  af_unix_disk_client_conf =
{
  1,  //           family: identifier of the socket family    */
  0,         /**< instance number within the family   */
  4, //        headerSize;       /* size of the header to read                 */
  0, //       msgLenOffset;     /* offset where the message length fits       */
  4, //        msgLenSize;       /* size of the message length field in bytes  */
  (1024*256), //        bufSize;         /* length of buffer (xmit and received)        */
  (300*1024), //        so_sendbufsize;  /* length of buffer (xmit and received)        */
  af_unix_disk_userRcvAllocBufCallBack,  //    userRcvAllocBufCallBack;   /* user callback for buffer allocation */
  af_unix_disk_response_callback,  //    userRcvCallBack;   /* callback provided by the connection owner block */
  af_unix_disk_disconnection_callback,  //    userDiscCallBack; /* callBack for TCP disconnection detection         */
  NULL,  //    userRcvReadyCallBack; /* NULL for default callback                    */
  NULL,  //    userXmitReadyCallBack; /* NULL for default callback                    */
  NULL,  //    userXmitEventCallBack; /* NULL for default callback                    */
  NULL,  //    *userRef;           /* user reference that must be recalled in the callbacks */
  NULL,  //    *xmitPool; /* user pool reference or -1 */
  NULL   //    *recvPool; /* user pool reference or -1 */
}; 


void * af_unix_disk_pool_send = NULL;
void * af_unix_disk_pool_recv = NULL;

/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
#define new_line(title)  pChar += sprintf(pChar,"\n%-24s |", title)
#define display_val(val) pChar += sprintf(pChar," %16lld |", (long long unsigned int) val)
#define display_div(val1,val2) if (val2==0) display_val(0);else display_val(val1/val2)
#define display_txt(txt) pChar += sprintf(pChar," %16s |", (char *) txt)

#define display_line_topic(title) \
  new_line(title);\
  for (i=0; i<=af_unix_disk_thread_count; i++) {\
    pChar += sprintf(pChar,"__________________|");\
  }
  
#define display_line_val(title,val) \
  new_line(title);\
  sum1 = 0;\
  for (i=0; i<af_unix_disk_thread_count; i++) {\
    sum1 += p[i].stat.val;\
    display_val(p[i].stat.val);\
  }
    
#define display_line_val_and_sum(title,val) \
  display_line_val(title,val);\
  display_val(sum1)

#define display_line_div(title,val1,val2) \
  new_line(title);\
  sum1 = sum2 = 0;\
  for (i=0; i<af_unix_disk_thread_count; i++) {\
    sum1 += p[i].stat.val1;\
    sum2 += p[i].stat.val2;\
    display_div(p[i].stat.val1,p[i].stat.val2);\
  }
  
#define display_line_div_and_sum(title,val1,val2) \
  display_line_div(title,val1,val2);\
  display_div(sum1,sum2)
  
void disk_thread_debug(char * argv[], uint32_t tcpRef, void *bufRef) {
  char           *pChar=uma_dbg_get_buffer();
  int i;
  uint64_t        sum1,sum2;
  rozofs_disk_thread_ctx_t *p = rozofs_disk_thread_ctx_tb;
  
  if (argv[1] != NULL) {
    if (strcmp(argv[1],"reset")==0) {
      for (i=0; i<af_unix_disk_thread_count; i++) {
	memset(&p[i].stat,0,sizeof(p[i].stat));
      }          
    }
  }
  new_line("Thread number");
  for (i=0; i<af_unix_disk_thread_count; i++) {
    display_val(p[i].thread_idx);
  }    
  display_txt("TOTAL");
  
  display_line_topic("Read Requests");  
  display_line_val_and_sum("   number", diskRead_count);
  display_line_val_and_sum("   Unknown cid/sid",diskRead_badCidSid);  
  display_line_val_and_sum("   error spare",diskRead_error_spare);  
  display_line_val_and_sum("   error",diskRead_error);  
  display_line_val_and_sum("   Bytes",diskRead_Byte_count);      
  display_line_val_and_sum("   Cumulative Time (us)",diskRead_time);
  display_line_div_and_sum("   Average Bytes",diskRead_Byte_count,diskRead_count);  
  display_line_div_and_sum("   Average Time (us)",diskRead_time,diskRead_count);
  display_line_div_and_sum("   Throughput (MBytes/s)",diskRead_Byte_count,diskRead_time);  
  
  display_line_topic("Write Requests");  
  display_line_val_and_sum("   number", diskWrite_count);
  display_line_val_and_sum("   Unknown cid/sid",diskWrite_badCidSid);  
  display_line_val_and_sum("   error",diskWrite_error);  
  display_line_val_and_sum("   Bytes",diskWrite_Byte_count);      
  display_line_val_and_sum("   Cumulative Time (us)",diskWrite_time);
  display_line_div_and_sum("   Average Bytes",diskWrite_Byte_count,diskWrite_count); 
  display_line_div_and_sum("   Average Time (us)",diskWrite_time,diskWrite_count);
  display_line_div_and_sum("   Throughput (MBytes/s)",diskWrite_Byte_count,diskWrite_time);  

  display_line_topic("Truncate Requests");  
  display_line_val_and_sum("   number", diskTruncate_count);
  display_line_val_and_sum("   Unknown cid/sid",diskTruncate_badCidSid);  
  display_line_val_and_sum("   error",diskTruncate_error);  
  display_line_val_and_sum("   Cumulative Time (us)",diskTruncate_time);
  display_line_div_and_sum("   Average Time (us)",diskTruncate_time,diskTruncate_count);
  
  display_line_topic("");  

  uma_dbg_send(tcpRef,bufRef,TRUE,uma_dbg_get_buffer());
}

/*__________________________________________________________________________
*/
/**
* test function for allocatiing a buffer in the client space

 The service might reject the buffer allocation because the pool runs
 out of buffer or because there is no pool with a buffer that is large enough
 for receiving the message because of a out of range size.

 @param userRef : pointer to a user reference: not used here
 @param socket_context_ref: socket context reference
 @param len : length of the incoming message
 
 @retval <>NULL pointer to a receive buffer
 @retval == NULL no buffer
*/
void * af_unix_disk_userRcvAllocBufCallBack(void *userRef,uint32_t socket_context_ref,uint32_t len) {
  return ruc_buf_getBuffer(af_unix_disk_pool_recv);   
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
void  af_unix_disk_disconnection_callback(void *userRef,uint32_t socket_context_ref,void *bufRef,int err_no) {

  fatal("af_unix_disk_disconnection_callback");
  
    
}

/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   Called from the socket controller when there is a response from a disk thread
   the response is either for a disk read or write
    
  @param socket_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
   @retval : TRUE-> xmit ready event expected
  @retval : FALSE-> xmit  ready event not expected
*/
void af_unix_disk_response_callback(void *userRef,uint32_t  socket_ctx_idx, void *recv_buf) {
  storio_disk_thread_request_e   opcode;
  storio_disk_thread_msg_t     * msg;
  rozorpc_srv_ctx_t            * rpcCtx;
  int                            ret;
  uint64_t                       tic, toc;  
  struct timeval                 tv;  
  
  msg = (storio_disk_thread_msg_t *) ruc_buf_getPayload(recv_buf);
  rpcCtx = msg->rpcCtx;
  opcode = msg->opcode;
  tic    = msg->timeStart;

  /*
  ** release the received buffer
  */
  ruc_buf_freeBuffer(recv_buf);
  
  switch (opcode) {
    case STORIO_DISK_THREAD_READ:
      STOP_PROFILING_IO(read,msg->size);
      break;
    case STORIO_DISK_THREAD_WRITE:
      STOP_PROFILING_IO(write,msg->size);
      break;     
    case STORIO_DISK_THREAD_TRUNCATE:
      STOP_PROFILING(truncate);
      break;   
    default:
      severe("Unexpected opcode %d", opcode);
  }
    
  ret = af_unix_generic_send_stream_with_idx((int)rpcCtx->socketRef,rpcCtx->xmitBuf);  
  if (ret == 0) {
    /**
    * success so remove the reference of the xmit buffer since it is up to the called
    * function to release it
    */
    ROZORPC_SRV_STATS(ROZORPC_SRV_SEND);
    rpcCtx->xmitBuf = NULL;
  }
  else {
    ROZORPC_SRV_STATS(ROZORPC_SRV_SEND_ERROR);
  }
    
  rozorpc_srv_release_context(rpcCtx);          
}

/*__________________________________________________________________________
*/
/**
*  Send a disk request to the disk threads
*
* @param opcode     the request operation code
* @param rpcCtx     pointer to the generic rpc context
* @param timeStart  time stamp when the request has been decoded
*
* @retval 0 on success -1 in case of error
*  
*/
int storio_disk_thread_intf_send(storio_disk_thread_request_e   opcode, 
                                 rozorpc_srv_ctx_t            * rpcCtx,
				 uint64_t                       timeStart) {
  storio_disk_thread_msg_t  * msg;
  char                      * dest_p;
  int                         ret;
  void                      * xmit_buf;

  /* allocate a buffer */
  xmit_buf = ruc_buf_getBuffer(af_unix_disk_pool_send);   
  if (xmit_buf == NULL) {
    errno = ENOMEM;
    severe("storio_disk_thread_intf_send %d out of buffer",opcode);
    return -1;
  }
 
  /* Get the buffer payload address*/
  msg = (storio_disk_thread_msg_t *)ruc_buf_getPayload(xmit_buf);

  /* Fill the message */
  msg->msg_len         = sizeof(storio_disk_thread_msg_t)-sizeof(msg->msg_len);
  msg->opcode          = opcode;
  msg->status          = 0;
  msg->transaction_id  = transactionId++;
  msg->timeStart       = timeStart;
  msg->size            = 0;
  msg->rpcCtx          = rpcCtx;

  /* Set the payload length */
  ruc_buf_setPayloadLen(xmit_buf,sizeof(storio_disk_thread_msg_t));

  /* Initialize the destination address in the buffer */
  dest_p = ruc_buf_get_usrDestInfo(xmit_buf);  
  strcpy(dest_p,destination_socketName);
  
  /* Send the buffer to its destination */
  ret = af_unix_generic_send_with_idx(af_unix_disk_south_socket_ref,xmit_buf);
  if (ret < 0) {
    severe("storio_disk_thread_intf_send sendto(%s) %s", ROZOFS_SOCK_FAMILY_DISK_NORTH, strerror(errno));
    return -1; 
  }
  return 0;
}
/*__________________________________________________________________________
* Initialize the disk thread interface
*
* @param hostname    storio hostname (for tests)
* @param nb_threads  Number of threads that can process the disk requests
* @param nb_buffer   Number of buffer for sending and number of receiving buffer
*
*  @retval 0 on success -1 in case of error
*/
int storio_disk_thread_intf_create(char * hostname, int nb_threads, int nb_buffer) {
  char socketName[128];

  af_unix_disk_thread_count = nb_threads;

  af_unix_disk_pool_send = ruc_buf_poolCreate(nb_buffer,sizeof(storio_disk_thread_msg_t));
  if (af_unix_disk_pool_send == NULL) {
    fatal("storio_disk_thread_intf_create af_unix_disk_pool_send (%d,%d)", nb_buffer, (int)sizeof(storio_disk_thread_msg_t));
    return -1;
  }
  ruc_buffer_debug_register_pool("diskSendPool",af_unix_disk_pool_send);   
  
  af_unix_disk_pool_recv = ruc_buf_poolCreate(1,sizeof(storio_disk_thread_msg_t));
  if (af_unix_disk_pool_recv == NULL) {
    fatal("storio_disk_thread_intf_create af_unix_disk_pool_recv (1,%d)", (int)sizeof(storio_disk_thread_msg_t));
    return -1;
  }
  ruc_buffer_debug_register_pool("diskRecvPool",af_unix_disk_pool_recv);   
   
  /*
  ** hostname is required for the case when several storaged run on the same server
  ** as is the case of test on one server only
  */ 
  sprintf(destination_socketName,"%s_%s", ROZOFS_SOCK_FAMILY_DISK_NORTH, hostname);
  
  sprintf(socketName,"%s_%s", ROZOFS_SOCK_FAMILY_DISK_SOUTH, hostname);
  af_unix_disk_south_socket_ref = af_unix_sock_create(socketName,&af_unix_disk_client_conf);
  if (af_unix_disk_south_socket_ref < 0) {
    fatal("storio_create_disk_thread_intf af_unix_sock_create(%s) %s",socketName, strerror(errno));
    return -1;
  }

  uma_dbg_addTopic("diskThreads", disk_thread_debug); 
   
  return storio_disk_thread_create(hostname, nb_threads);
}
