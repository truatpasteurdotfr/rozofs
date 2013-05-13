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

#ifndef EXPGW_EXPORT_H
#define EXPGW_EXPORT_H


#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>



#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <rozofs/core/ruc_list.h>
#include <errno.h>


#include <rozofs/rozofs.h>
#include "config.h"
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/core/rozofs_tx_common.h>
#include <rozofs/core/north_lbg_api.h>
#include <rpc/rpc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/core/expgw_common.h>

#if 0
typedef enum
{
   EXPGW_PORT_ROZOFSMOUNT_IDX= 0,
   EXPGW_PORT_EXPORTD_IDX,
   EXPGW_PORT_DEBUG_IDX,
   EXPGW_PORT_MAX_IDX,  
} expgw_listening_ports_e;
#endif
/**
*  STORCLI Resource configurationj
*/
#define EXPGW_CNF_NO_BUF_CNT          1
#define EXPGW_CNF_NO_BUF_SZ           1




/**
*  Macro METADATA start non blocking case
*/
#define START_PROFILING_EXPGW(buffer,the_probe)\
{ \
  unsigned long long time;\
  struct timeval     timeDay;  \
  if (buffer != NULL)\
  { \
   gprofiler.the_probe[P_COUNT]++;\
   buffer->profiler_probe = &gprofiler.the_probe[0]; \
   gettimeofday(&timeDay,(struct timezone *)0);  \
   time = MICROLONG(timeDay); \
   buffer->profiler_time =(uint64_t)time ;\
  }\
}

/**
*  Macro METADATA stop non blocking case
*/
#define STOP_PROFILING_EXPGW(buffer)\
{ \
  unsigned long long timeAfter;\
  struct timeval     timeDay;  \
  if (buffer->profiler_probe != NULL)\
  { \
    gettimeofday(&timeDay,(struct timezone *)0);  \
    timeAfter = MICROLONG(timeDay); \
    buffer->profiler_probe[P_ELAPSE] += (timeAfter-buffer->profiler_time); \
    buffer->profiler_probe = NULL;\
  }\
}

/**
*  North Interface
*/
#define EXPGW_CTX_CNT   8  /**< context for processing either a read or write request from rozofsmount and internal read req */
#define EXPGW_CTX_MIN_CNT 3 /**< minimum count to process a request from rozofsmount */
/**
* Buffer s associated with the reception of the load balancing group on north interface
*/
#define EXPGW_NORTH_LBG_BUF_RECV_CNT   EXPGW_CTX_CNT  /**< number of reception buffer to receive from rozofsmount */
#define EXPGW_NORTH_LBG_BUF_RECV_SZ    (1024*38)  /**< max user data payload is 38K       */
/**
* Storcli buffer mangement configuration
*  INTERNAL_READ are small buffer used when a write request requires too trigger read of first and/or last block
*/
#define EXPGW_NORTH_MOD_INTERNAL_READ_BUF_CNT   EXPGW_CTX_CNT  /**< expgw_north_small_buf_count  */
#define EXPGW_NORTH_MOD_INTERNAL_READ_BUF_SZ   1024  /**< expgw_north_small_buf_sz  */

#define EXPGW_NORTH_MOD_XMIT_BUF_CNT   EXPGW_CTX_CNT  /**< expgw_north_large_buf_count  */
#define EXPGW_NORTH_MOD_XMIT_BUF_SZ    EXPGW_NORTH_LBG_BUF_RECV_SZ  /**< expgw_north_large_buf_sz  */

#define EXPGW_SOUTH_TX_XMIT_BUF_CNT   (EXPGW_CTX_CNT)  /**< expgw_south_large_buf_count  */
#define EXPGW_SOUTH_TX_XMIT_BUF_SZ    (1024*8)                           /**< expgw_south_large_buf_sz  */

/**
* configuartion of the resource of the transaction module
*
*  concerning the buffers, only reception buffer are required 
*/

#define EXPGW_SOUTH_TX_CNT   (EXPGW_CTX_CNT)  /**< number of transactions towards storaged  */
#define EXPGW_SOUTH_TX_RECV_BUF_CNT   EXPGW_SOUTH_TX_CNT  /**< number of recption buffers  */
#define EXPGW_SOUTH_TX_RECV_BUF_SZ    EXPGW_SOUTH_TX_XMIT_BUF_SZ  /**< buffer size  */

#define EXPGW_NORTH_TX_BUF_CNT   0  /**< not needed for the case of storcli  */
#define EXPGW_NORTH_TX_BUF_SZ    0  /**< buffer size  */


#define EXPGW_MAX_RETRY   3  /**< max attempts for read or write on mstorage */



#define EXPGW_RPC_MAX_DECODE_BUFFER  256

typedef	union {
		ep_lookup_arg_t ep_lookup_1_arg;
		ep_mfile_arg_t ep_getattr_1_arg;
		ep_setattr_arg_t ep_setattr_1_arg;
		ep_mfile_arg_t ep_readlink_1_arg;
		ep_mknod_arg_t ep_mknod_1_arg;
		ep_mkdir_arg_t ep_mkdir_1_arg;
		ep_unlink_arg_t ep_unlink_1_arg;
		ep_rmdir_arg_t ep_rmdir_1_arg;
		ep_symlink_arg_t ep_symlink_1_arg;
		ep_rename_arg_t ep_rename_1_arg;
		ep_io_arg_t ep_read_block_1_arg;
		ep_write_block_arg_t ep_write_block_1_arg;
		ep_link_arg_t ep_link_1_arg;
		ep_setxattr_arg_t ep_setxattr_1_arg;
		ep_getxattr_arg_t ep_getxattr_1_arg;
		ep_removexattr_arg_t ep_removexattr_1_arg;
		ep_listxattr_arg_t ep_listxattr_1_arg;
	} expgw_argument;



/**
* common context for export request received from rozofsmount
*/
typedef struct _expgw_ctx_t
{
  ruc_obj_desc_t link;
  uint32_t            index;         /**< Index of the MS */
  uint32_t            free;          /**< Is the context free or allocated TRUE/FALSE */
  uint32_t            integrity;     /**< the value of this field is incremented at  each MS ctx allocation  */
  uint32_t            opcode;      /**< opcode associated with the request, when the key is not used the value is EXPGW_NULL */
  uint32_t   src_transaction_id;  /**< transaction id of the source request                                       */
  uint64_t            timestamp;
  void      *recv_buf;             /**< pointer to the receive buffer that carries the request        */
  uint32_t   socketRef;            /**< reference of the socket on which the answser must be sent     */
  sys_recv_pf_t    response_cbk;  /**< callback function associated with the response of the root transaction */
  xdrproc_t        xdr_result;
  xdrproc_t        xdr_result_internal;
  int        xdr_offset_in_req;   /**< offset oif the first applicative byte in a request -> in recv_buf  */  
  void      *xmitBuf;             /**< reference of the xmit buffer that will use for sending the response with an errcode       */
  void     *fid_cache_entry;      /**< pointer to a pre-allocated cache entry for fid: found in case of miss */
  void     *decoded_arg;          /**< pointer to the decoded argument */
  uint64_t *profiler_probe;       /**< pointer to the profiler counter */
  uint64_t profiler_time;        /**< profiler timestamp */
   /*
   ** Parameters specific to the exportd gateway management
   */
   expgw_tx_routing_ctx_t expgw_routing_ctx; 
} expgw_ctx_t;


#define EXPGW_START_NORTH_PROF(buffer)\
 { \
  unsigned long long time;\
  struct timeval     timeDay;  \
  uint64_t *gprofiler_fake = &gprofiler.null[0]; \
  gprofiler_fake += (buffer->opcode*(P_ELAPSE+1)); \
  gprofiler_fake[P_COUNT]++;\
  if (buffer != NULL)\
    {\
        gettimeofday(&timeDay,(struct timezone *)0);  \
        time = MICROLONG(timeDay); \
        (buffer)->timestamp =time;\
    }\
}
/**
*  Macro METADATA stop non blocking case
*/
#define EXPGW_STOP_NORTH_PROF(buffer)\
{ \
  unsigned long long timeAfter;\
  struct timeval     timeDay;  \
  uint64_t *gprofiler_fake = &gprofiler.null[0]; \
  gprofiler_fake += ((buffer)->opcode*(P_ELAPSE+1)); \
  if (buffer != NULL)\
  { \
    gettimeofday(&timeDay,(struct timezone *)0);  \
    timeAfter = MICROLONG(timeDay); \
    gprofiler_fake[P_ELAPSE] += (timeAfter-(buffer)->timestamp); \
  }\
}



#define EXPGW_ERR_PROF(the_probe)\
 { \
  gprofiler.the_probe[P_COUNT]++;\
}





/**
* transaction statistics
*/
typedef enum 
{
  EXPGW_SEND =0 ,
  EXPGW_SEND_ERROR,
  EXPGW_RECV_OK, 
  EXPGW_RECV_OUT_SEQ,
  EXPGW_TIMEOUT,
  EXPGW_ENCODING_ERROR,
  EXPGW_DECODING_ERROR,
  EXPGW_NO_CTX_ERROR,
  EXPGW_NO_BUFFER_ERROR,
  EXPGW_COUNTER_MAX
}expgw_tx_stats_e;

extern uint64_t expgw_stats[];

#define EXPGW_STATS(counter) expgw_stats[counter]++;

/**
* Buffers information
*/

extern int expgw_north_buf_count;
extern int expgw_north_small_buf_sz;
extern int expgw_north_large_buf_count;
extern int expgw_north_large_buf_sz;
extern int expgw_south_small_buf_count;
extern int expgw_south_small_buf_sz;
extern int expgw_south_large_buf_count;
extern int expgw_south_large_buf_sz;


extern uint32_t expgw_seqnum ;


/**
* Buffer Pools
*/
typedef enum 
{
  _EXPGW_NORTH_SMALL_POOL =0 ,
  _EXPGW_NORTH_LARGE_POOL, 
  _EXPGW_SOUTH_SMALL_POOL,
  _EXPGW_SOUTH_LARGE_POOL,
  _EXPGW_MAX_POOL
} expgw_buffer_pool_e;

extern void *expgw_pool[];


#define EXPGW_NORTH_SMALL_POOL expgw_pool[_EXPGW_NORTH_SMALL_POOL]
#define EXPGW_NORTH_LARGE_POOL expgw_pool[_EXPGW_NORTH_LARGE_POOL]
#define EXPGW_SOUTH_SMALL_POOL expgw_pool[_EXPGW_SOUTH_SMALL_POOL]
#define EXPGW_SOUTH_LARGE_POOL expgw_pool[_EXPGW_SOUTH_LARGE_POOL]

/*
**__________________________________________________________________________
*/
/**
  allocate a  context to handle a client read/write transaction

  @param     : none
  @retval <>NULL pointer to the allocated context
  @retval NULL out of free context
*/
expgw_ctx_t *expgw_alloc_context();

/*
**__________________________________________________________________________
*/
/**
* release a read/write context that has been use for either a read or write operation

  @param : pointer to the context
  
  @retval <>NULL pointer to the allocated context
  @retval NULL out of free context
*/
void expgw_release_context(expgw_ctx_t *ctx_p);


extern uint32_t    expgw_ctx_count;           /**< Max number of contexts    */
extern uint32_t    expgw_ctx_allocated;      /**< current number of allocated context        */
/*
**__________________________________________________________________________
*/
/**
*  Get the number of free transaction context
*
  @param none
  
  @retval number of free context
*/
static inline uint32_t expgw_get_free_transaction_context()
{
  return(expgw_ctx_count - expgw_ctx_allocated);

}
/*
**__________________________________________________________________________
*/
/**
* API to forward a request that has been received on from rozofsmount towards exportd
  The buffer used is the buffer stored in the recv_buf field of the context.
  
  Prior to forward the message to the exportd, the following fields of the rpc message are modified
    - rpc transaction id (xid)
    - number of export gateways (user part)
    - index of the current export gateway (user part of the rpc message)

  In order to avoid a decoding of the message, the context has been updated with the offset on
  the "number of export gateways" of the user message. The index of the current export gateway
  must then be the next field after "number of export gateways"


 @param lbg_id     : reference of the load balancing group
 @param seqnum     : unused
 @param opaque_value_idx1 : unused
 @param recv_cbk   : receive callback function

 @param user_ctx_p : pointer to the working context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */

int expgw_forward_rq_common(expgw_ctx_t *working_ctx_p,
                                 uint32_t lbg_id,
                                 uint32_t seqnum,
                                 uint32_t opaque_value_idx1,  
                                 sys_recv_pf_t recv_cbk,void *user_ctx_p);


/*
**__________________________________________________________________________
*/
/**
* send a read pr write success reply
  insert the transaction_id associated with the inittial request transaction id
  
  In case of a success it is up to the called function to release the xmit buffer
  
  @param p : pointer to the root transaction context used for the read
  
  @retval none

*/
void expgw_common_reply_forward(expgw_ctx_t *p);

/*
**__________________________________________________________________________
*/
/**
* send a error read reply
  That API fill up the common header with the SP_READ_RSP opcode
  insert the transaction_id associated with the inittial request transaction id
  insert a status OK
  insert the length of the data payload
  
  In case of a success it is up to the called function to release the xmit buffer
  
  @param p : pointer to the root transaction context used for the read
  @param error : error code
  
  @retval none

*/
void expgw_reply_error(expgw_ctx_t *p,int error);


/*
**__________________________________________________________________________
*/
/**
* send a rpc reply: the encoding function MUST be found in xdr_result 
 of the gateway context

  It is assumed that the xmitBuf MUST be found in xmitBuf field
  
  In case of a success it is up to the called function to release the xmit buffer
  
  @param p : pointer to the root transaction context used for the read
  @param arg_ret : returned argument to encode 
  
  @retval none

*/
void expgw_forward_reply (expgw_ctx_t *p,char * arg_ret);

/*
**__________________________________________________________________________
*/
/**
* API for creation a transaction towards an exportd

 The reference of the north load balancing is extracted for the client structure
 fuse_ctx_p:
 That API needs the pointer to the current fuse context. That nformation will be
 saved in the transaction context as userParam. It is intended to be used later when
 the client gets the response from the server
 encoding function;
 For making that API generic, the caller is intended to provide the function that
 will encode the message in XDR format. The source message that is encoded is 
 supposed to be pointed by msg2encode_p.
 Since the service is non-blocking, the caller MUST provide the callback function 
 that will be used for decoding the message
 

 @param lbg_id     : reference of the load balancing group of the exportd
 @param prog       : program
 @param vers       : program version
 @param opcode     : metadata opcode
 @param encode_fct : encoding function
 @msg2encode_p     : pointer to the message to encode
 @param recv_cbk   : receive callback function
 @param ctx_p      : pointer to the user context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */

int expgw_export_build_and_send_common(int lbg_id,uint32_t prog,uint32_t vers,
                                       int opcode,xdrproc_t encode_fct,void *msg2encode_p,
                                       sys_recv_pf_t recv_cbk,void *ctx_p) ;


/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated gateway context
 
 @return none
 */

void expgw_generic_export_reply_cbk(void *this,void *param) ;

/*
**__________________________________________________________________
*/
/**
* API for re-sending a rpc message towards an exportd
 Here it is assumed that the transaction context is already allocated and ready to use
 The routing context has a buffer available and is already encoded (xdr)
 
 Only the transaction xid of the rpc message will be changed.
 
 note : that function is intended to be called by expgw_generic_export_reply_cbk()

 @param rozofs_tx_ctx_p        : transaction context
 @param recv_cbk        : callback function (may be NULL)
 @param req_ctx_p       : exportd gateway context (associated with a request comming from either an export gateway or rozofsmount)
 @param vers       : program version
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */
int expgw_resend_routing_common(rozofs_tx_ctx_t *rozofs_tx_ctx_p, sys_recv_pf_t recv_cbk,expgw_ctx_t *req_ctx_p);

/*
**__________________________________________________________________
*/
/**
* API for creation a transaction towards an exportd


 @param eid     : reference of the export
 @param fid     : unique reference of a filesystem object (file, directory, etc...)
 @param prog       : program
 @param vers       : program version
 @param opcode     : metadata opcode
 @param encode_fct : encoding function
 @msg2encode_p     : pointer to the message to encode
 @param recv_cbk   : receive callback function
 @param ctx_p      : pointer to the user context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */
int expgw_export_build_and_route_common(uint32_t eid,fid_t fid,uint32_t prog,uint32_t vers,
                                       int opcode,xdrproc_t encode_fct,void *msg2encode_p,
                                       sys_recv_pf_t recv_cbk,void *ctx_p);



/*
**__________________________________________________________________________
*/
/**
* API to forward a request that has been received on from rozofsmount towards exportd
  The buffer used is the buffer stored in the recv_buf field of the context.
  
  Prior to forward the message to the exportd, the following fields of the rpc message are modified
    - rpc transaction id (xid)
    - number of export gateways (user part)
    - index of the current export gateway (user part of the rpc message)

  In order to avoid a decoding of the message, the context has been updated with the offset on
  the "number of export gateways" of the user message. The index of the current export gateway
  must then be the next field after "number of export gateways"


 @param eid     : reference of the export
 @param fid     : unique reference of a filesystem object (file, directory, etc...)
 @param seqnum     : unused
 @param opaque_value_idx1 : unused
 @param recv_cbk   : receive callback function

 @param user_ctx_p : pointer to the working context
 
 @retval 0 on success;
 @retval -1 on error,, errno contains the cause
 */

int expgw_routing_rq_common(expgw_ctx_t *working_ctx_p,
                                 uint32_t eid,fid_t fid,
                                 uint32_t seqnum,
                                 uint32_t opaque_value_idx1,  
                                 sys_recv_pf_t recv_cbk,void *user_ctx_p);
/*
**__________________________________________________________________________
*/
/**
* send a error read reply
  That API fill up the common header with the SP_READ_RSP opcode
  insert the transaction_id associated with the inittial request transaction id
  insert a status OK
  insert the length of the data payload
  
  In case of a success it is up to the called function to release the xmit buffer
  
  @param p : pointer to the root transaction context used for the read
  @param eid : reference of the eid
  
  @retval none

*/
void expgw_reply_error_no_such_eid(expgw_ctx_t *p,int eid);
/*
**__________________________________________________________________________
*/
/**
   expgw_module_init

  create the Transaction context pool


@retval   : RUC_OK : done
@retval          RUC_NOK : out of memory
*/
uint32_t expgw_module_init();
/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   THis the callback that is activated upon the recption of a metadata operation
    from a rozofsmount client 

    
  @param socket_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
   @retval : TRUE-> xmit ready event expected
  @retval : FALSE-> xmit  ready event not expected
*/
void expgw_rozofs_req_rcv_cbk(void *userRef,uint32_t  socket_ctx_idx, void *recv_buf);


/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   THis the callback that is activated upon the recption of a exportd cache
   operation from an exportd client 

    
  @param socket_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
   @retval : TRUE-> xmit ready event expected
  @retval : FALSE-> xmit  ready event not expected
*/
void expgw_exportd_req_rcv_cbk(void *userRef,uint32_t  socket_ctx_idx, void *recv_buf);


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

int expgw_rozofs_north_interface_init(uint32_t src_ipaddr_host,uint16_t src_port_host,
                             int read_write_buf_count,int read_write_buf_sz);
#endif
