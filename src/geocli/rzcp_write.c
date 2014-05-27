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

void rzcp_write_cbk(void *this,void *param) ;
/*
**__________________________________________________________________
*/
/** 
    send a write request towards storcli
    it is assumed that the following parameters have been set in the
    write part of the context:
    off_cur : write offset
    len_cur : current length to write
    
    The main context must have valid information for the following fields:
    rzcp_copy_cbk : call upon end of the write request.
    shared_buf_ref and shared_buf_idx : respectively the shared buffer that contains the data and its ref
    storcli_idx : index of the storcli
 
  @param *cpy_p: pointer to copy context
  
  @retval none
*/
 
int rzcp_write_req(rzcp_copy_ctx_t * cpy_p) 
{
   storcli_write_arg_t  args;
   int ret;
   rzcp_file_ctx_t *rw_ctx_p;

    rw_ctx_p = &cpy_p->write_ctx;
    // Fill request
    args.cid    = rw_ctx_p->cid;
    args.layout = rw_ctx_p->layout;
    memcpy(args.dist_set, rw_ctx_p->sids, sizeof (sid_t) * ROZOFS_SAFE_MAX);
    memcpy(args.fid, rw_ctx_p->fid, sizeof (fid_t));
    args.off = rw_ctx_p->off_cur;
    args.data.data_len = rw_ctx_p->len_cur;
    
    /*
    ** If file was empty at opening tell it to storcli at 1rts write: it is the default
    ** mode for copying. However it might more interesting to insert it on the first
    ** write in the case of the geo-replication
     */
      args.empty_file = 1;
    /*
    ** get the storcli to use for the transaction
    */
    uint32_t *p32;
    void *shared_buf_ref = cpy_p->shared_buf_ref[SHAREMEM_IDX_WRITE];
    if (shared_buf_ref == NULL)
    {
       /*
       ** the shared memory i smandatory for rzcopy module
       */
       errno = ENOMEM;
       ret = -1;
       goto error;
    }
    /*
    ** clear the first 4 bytes of the array that is supposed to contain
    ** the reference of the transaction
    */
     p32 = (uint32_t *)ruc_buf_getPayload(shared_buf_ref);
     *p32 = 0;
     /*
     ** store the length to write 
     */
     p32[1] = args.data.data_len;
     /*
     ** indicate to the transmitter that shared memory request type must be used
     */
     args.data.data_len = 0x80000000 | cpy_p->shared_buf_idx[SHAREMEM_IDX_WRITE]; 
    /*
    ** now initiates the transaction towards the remote end
    */
    ret = rozofs_storcli_send_common(NULL,ROZOFS_TMR_GET(TMR_STORCLI_PROGRAM),STORCLI_PROGRAM, STORCLI_VERSION,
                              STORCLI_WRITE,
			      (cpy_p->shared_buf_idx[SHAREMEM_IDX_WRITE]!=-1)?
			      (xdrproc_t) xdr_storcli_write_arg_no_data_t: (xdrproc_t)xdr_storcli_write_arg_t,
			      (void *)&args,
                              rzcp_write_cbk,cpy_p,cpy_p->storcli_idx,rw_ctx_p->fid); 
    if (ret < 0) goto error;
    

    return ret;    
error:
    return ret;

}


/*
**__________________________________________________________________
*/
/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated rzcopy context
 
 @return none
 */
void rzcp_write_cbk(void *this,void *param) 
{
   struct rpc_msg  rpc_reply;
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   storcli_status_ret_t ret;
   xdrproc_t decode_proc = (xdrproc_t)xdr_storcli_status_ret_t;
   errno = 0;
   rpc_reply.acpted_rply.ar_results.proc = NULL;

   rzcp_copy_ctx_t *cpy_p = (rzcp_copy_ctx_t*)param;
       
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
       severe(" transaction error %s",strerror(errno));
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
       severe(" transaction error %s",strerror(errno));
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
     severe(" transaction error %s",strerror(errno));
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
       severe(" transaction error %s",strerror(errno));
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
    ** release the received buffer and the transaction context
    */
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p); 
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);  
    /*
    ** lock the shared buffer to avoid storcli from using it
    */ 
    if (cpy_p->shared_buf_ref[SHAREMEM_IDX_WRITE]!= NULL) 
    {
      uint32_t *p32 = (uint32_t*)ruc_buf_getPayload(cpy_p->shared_buf_ref[SHAREMEM_IDX_WRITE]);    
      /*
      ** clear the timestamp
      */
      *p32 = 0;
    }
    /*
    ** inform the caller thanks its callback
    */
    (*cpy_p->rzcp_caller_cbk)(cpy_p,status);
    return;
error:
    status = -1;
    goto out;
    return;
}

