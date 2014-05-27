/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This cpy_p is part of Rozofs.

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
#include <inttypes.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/storcli_proto.h>
#include <rozofs/core/rozofs_tx_api.h>
#include "rozofs_sharedmem.h"
#include "rozofs_rw_load_balancing.h"
#include "rzcp_file_ctx.h"
#include <rozofs/rozofs_timer_conf.h>
#include <rozofs/rpc/rozofs_rpc_util.h>
#include "rozofsmount.h"
#include "rzcp_copy.h"


void rzcp_remove_cbk(void *this,void *param) ;

/*
**_________________________________________________________________________
*/
/** 
 *  delete a file 
 * @param *f: pointer to the cpy_p structure
 * @param off: offset to read from
 * @param *buf: pointer where the data will be stored: buffer associated with the cpy_p_t structure
 * @param len: length to read: (correspond to the max buffer size defined in the exportd parameters
 * @param *last_block_size_p: pointer to store the size of the last block size
 *  read
 *
 * @return: the length read on success, -1 otherwise (errno is set)
 */
 
int rzcp_remove_req(rzcp_copy_ctx_t * cpy_p) 
{
   storcli_delete_arg_t  args;
   int ret;
   rzcp_file_ctx_t *rw_ctx_p;

   rw_ctx_p= &cpy_p->read_ctx;

    // Fill request
    args.cid = rw_ctx_p->cid;
    args.layout = rw_ctx_p->layout;
    memcpy(args.dist_set, rw_ctx_p->sids, sizeof (sid_t) * ROZOFS_SAFE_MAX);
    memcpy(args.fid, rw_ctx_p->fid, sizeof (fid_t));
    /*
    ** now initiates the transaction towards the remote end
    */
    ret = rozofs_storcli_send_common(NULL,ROZOFS_TMR_GET(TMR_STORCLI_PROGRAM),STORCLI_PROGRAM, STORCLI_VERSION,
                              STORCLI_DELETE,(xdrproc_t) xdr_storcli_delete_arg_t,(void *)&args,
                              rzcp_remove_cbk,cpy_p,cpy_p->storcli_idx,rw_ctx_p->fid); 
    if (ret < 0) goto error;
    
    /*
    ** no error just waiting for the answer
    */
    return ret;    
error:
    return ret;

}

/*
**_________________________________________________________________________
*/
/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated pseudo cpy_p context
 
 @return none
 */
void rzcp_remove_cbk(void *this,void *param) 
{
   struct rpc_msg  rpc_reply;
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   storcli_status_ret_t ret;
   xdrproc_t decode_proc = (xdrproc_t)xdr_storcli_status_ret_t;
   errno =0;

   rzcp_copy_ctx_t *cpy_p = (rzcp_copy_ctx_t*)param;
   
   rpc_reply.acpted_rply.ar_results.proc = NULL;
    /*
    ** get the pointer to the transaction context:
    ** it is required to get the information related to the receive buffer
    */
    rozofs_tx_ctx_t      *rozofs_tx_ctx_p = (rozofs_tx_ctx_t*)this;     
    /*    
    ** get the status of the transaction -> 0 OK, -1 error (need to get errno for source cause
    */
    status = rozofs_tx_get_status(this);
    if (status < 0)
    {
       /*
       ** something wrong happened
       */
       errno = rozofs_tx_get_errno(this);  
       goto error; 
    }
    /*
    ** get the pointer to the receive buffer payload
    */
    recv_buf = rozofs_tx_get_recvBuf(this);
    if (recv_buf == NULL)
    {
       /*
       ** something wrong happened
       */
       errno = EFAULT;  
       goto error;         
    }
    payload  = (uint8_t*) ruc_buf_getPayload(recv_buf);
    payload += sizeof(uint32_t); /* skip length*/
    /*
    ** OK now decode the received message
    */
    bufsize = (int) ruc_buf_getPayloadLen(recv_buf);
    xdrmem_create(&xdrs,(char*)payload,bufsize,XDR_DECODE);
    /*
    ** decode the rpc part
    */
    if (rozofs_xdr_replymsg(&xdrs,&rpc_reply) != TRUE)
    {
     TX_STATS(ROZOFS_TX_DECODING_ERROR);
     errno = EPROTO;
     goto error;
    }
    /*
    ** ok now call the procedure to decode the message
    */
    memset(&ret,0, sizeof(ret));                    
    if (decode_proc(&xdrs,&ret) == FALSE)
    {
       TX_STATS(ROZOFS_TX_DECODING_ERROR);
       errno = EPROTO;
       xdr_free((xdrproc_t) decode_proc, (char *) &ret);
       goto error;
    }   
    if (ret.status == STORCLI_FAILURE) {
        errno = ret.storcli_status_ret_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        goto error;
    }
    xdr_free((xdrproc_t) decode_proc, (char *) &ret); 
    /*
    ** success 
    */
    status = 0;
out:
    /*
    ** release the transaction context 
    */
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);  
    /*
    ** inform the caller thanks its callback
    */
    (*cpy_p->rzcp_caller_cbk)(cpy_p,status);
    return;
error:
    status = -1;
    goto out;
}

