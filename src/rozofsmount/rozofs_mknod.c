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

#include <rozofs/rpc/eproto.h>

#include "rozofs_fuse_api.h"

DECLARE_PROFILING(mpp_profiler_t);


int rozofs_expgateway_send_routing_common_mknod(uint32_t eid,fid_t fid,uint32_t prog,uint32_t vers,
                              int opcode,xdrproc_t encode_fct,void *msg2encode_p,
                              sys_recv_pf_t recv_cbk,void *fuse_buffer_ctx_p) 
{
    DEBUG_FUNCTION;
   
    uint8_t           *arg_p;
    uint32_t          *header_size_p;
    rozofs_tx_ctx_t   *rozofs_tx_ctx_p = NULL;
    void              *xmit_buf = NULL;
    int               bufsize;
    int               ret;
    int               position;
    XDR               xdrs;    
	struct rpc_msg   call_msg;
    uint32_t         null_val = 0;
    int lbg_id;
    expgw_tx_routing_ctx_t local_routing_ctx;
    expgw_tx_routing_ctx_t  *routing_ctx_p;
    
    rozofs_fuse_save_ctx_t *fuse_ctx_p=NULL;
    

    if (fuse_buffer_ctx_p != NULL) {
      GET_FUSE_CTX_P(fuse_ctx_p,fuse_buffer_ctx_p);
    }  
    if (fuse_ctx_p != NULL) {    
      routing_ctx_p = &fuse_ctx_p->expgw_routing_ctx ;
    }
    else {
      routing_ctx_p = &local_routing_ctx;
    }  

    /*
    ** get the available load balancing group(s) for routing the request 
    */    
    ret  = expgw_get_export_routing_lbg_info(eid,fid,routing_ctx_p);
    if (ret < 0)
    {
      /*
      ** no load balancing group available
      */
      errno = EPROTO;
      goto error;    
    }

    /*
    ** allocate a transaction context
    */
    rozofs_tx_ctx_p = rozofs_tx_alloc();  
    if (rozofs_tx_ctx_p == NULL) 
    {
       /*
       ** out of context
       ** --> put a pending list for the future to avoid repluing ENOMEM
       */
       TX_STATS(ROZOFS_TX_NO_CTX_ERROR);
       errno = ENOMEM;
       goto error;
    }    
    /*
    ** allocate an xmit buffer
    */  
    xmit_buf = ruc_buf_getBuffer(ROZOFS_TX_SMALL_TX_POOL);
    if (xmit_buf == NULL)
    {
      /*
      ** something rotten here, we exit we an error
      ** without activating the FSM
      */
      TX_STATS(ROZOFS_TX_NO_BUFFER_ERROR);
      errno = ENOMEM;
      goto error;
    } 

    /*
    ** The system attempts first to forward the message toward load balancing group
    ** of an export gateway and then to the master export if the load balancing group
    ** of the export gateway is not available
    */
    lbg_id = expgw_routing_get_next(routing_ctx_p,xmit_buf);
    /*
    ** store the reference of the xmit buffer in the transaction context: might be useful
    ** in case we want to remove it from a transmit list of the underlying network stacks
    */
    rozofs_tx_save_xmitBuf(rozofs_tx_ctx_p,xmit_buf);
    /*
    ** get the pointer to the payload of the buffer
    */
    header_size_p  = (uint32_t*) ruc_buf_getPayload(xmit_buf);
    arg_p = (uint8_t*)(header_size_p+1);  
    /*
    ** create the xdr_mem structure for encoding the message
    */
    bufsize = rozofs_tx_get_small_buffer_size();
    xdrmem_create(&xdrs,(char*)arg_p,bufsize,XDR_ENCODE);
    /*
    ** fill in the rpc header
    */
    call_msg.rm_direction = CALL;
    /*
    ** allocate a xid for the transaction 
    */
	call_msg.rm_xid             = rozofs_tx_alloc_xid(rozofs_tx_ctx_p); 
	call_msg.rm_call.cb_rpcvers = RPC_MSG_VERSION;
	/* XXX: prog and vers have been long historically :-( */
	call_msg.rm_call.cb_prog = (uint32_t)prog;
	call_msg.rm_call.cb_vers = (uint32_t)vers;
	if (! xdr_callhdr(&xdrs, &call_msg))
    {
       /*
       ** THIS MUST NOT HAPPEN
       */
       TX_STATS(ROZOFS_TX_ENCODING_ERROR);
       errno = EPROTO;
       goto error;	
    }
    /*
    ** insert the procedure number, NULL credential and verifier
    */
    XDR_PUTINT32(&xdrs, (int32_t *)&opcode);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
    XDR_PUTINT32(&xdrs, (int32_t *)&null_val);
        
    /*
    ** ok now call the procedure to encode the message
    */
    if ((*encode_fct)(&xdrs,msg2encode_p) == FALSE)
    {
       TX_STATS(ROZOFS_TX_ENCODING_ERROR);
       errno = EPROTO;
       goto error;
    }
    /*
    ** Now get the current length and fill the header of the message
    */
    position = XDR_GETPOS(&xdrs);
    /*
    ** update the length of the message : must be in network order
    */
    *header_size_p = htonl(0x80000000 | position);
    /*
    ** set the payload length in the xmit buffer
    */
    int total_len = sizeof(*header_size_p)+ position;
    ruc_buf_setPayloadLen(xmit_buf,total_len);
    /*
    ** store the receive call back and its associated parameter
    */
    rozofs_tx_ctx_p->recv_cbk   = recv_cbk;
    rozofs_tx_ctx_p->user_param = fuse_buffer_ctx_p;    
    /*
    ** now send the message
    */

reloop:
    ret = north_lbg_send(lbg_id,xmit_buf);
#warning stop profiling
    STOP_PROFILING_NB(fuse_buffer_ctx_p,rozofs_ll_mknod);
    if (ret < 0)
    {
       TX_STATS(ROZOFS_TX_SEND_ERROR);
       /*
       ** attempt to get the next available load balancing group
       */
       lbg_id = expgw_routing_get_next(routing_ctx_p,xmit_buf);
       if (lbg_id >= 0) goto reloop;
       
       errno = EFAULT;
      goto error;  
    }
    TX_STATS(ROZOFS_TX_SEND);

    /*
    ** OK, so now finish by starting the guard timer
    */
    if (opcode == EP_STATFS) {
      /* df must give a response (even negative) in less than 2 seconds !!! */
      rozofs_tx_start_timer(rozofs_tx_ctx_p, 2);    
    }
    else {
      rozofs_tx_start_timer(rozofs_tx_ctx_p, ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM));
    }  
    return 0;  
    
  error:
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);
    return -1;    
}



/*
**__________________________________________________________________
*/
/**
 * Create file node
 *
 * Create a regular file, character device, block device, fifo or
 * socket node.
 *
 * Valid replies:
 *   fuse_reply_entry
 *   fuse_reply_err
 *
 * @param req request handle
 * @param parent inode number of the parent directory
 * @param name to create
 * @param mode file type and mode with which to create the new file
 * @param rdev the device number (only valid if created file is a device)
 */

void rozofs_ll_mknod_cbk(void *this,void *param);
 
void rozofs_ll_mknod_nb(fuse_req_t req, fuse_ino_t parent, const char *name,
        mode_t mode, dev_t rdev) 
{
    ientry_t *ie = 0;
    const struct fuse_ctx *ctx;
    ctx = fuse_req_ctx(req);
    epgw_mknod_arg_t arg;

    int    ret;
    int trc_idx = rozofs_trc_req_name(srv_rozofs_ll_mknod,parent,(char*)name);
    void *buffer_p = NULL;
    /*
    ** allocate a context for saving the fuse parameters
    */
    buffer_p = rozofs_fuse_alloc_saved_context();
    if (buffer_p == NULL)
    {
      severe("out of fuse saved context");
      errno = ENOMEM;
      goto error;
    }
    SAVE_FUSE_PARAM(buffer_p,req);
    SAVE_FUSE_PARAM(buffer_p,parent);
    SAVE_FUSE_STRING(buffer_p,name);
    SAVE_FUSE_PARAM(buffer_p,mode);
    SAVE_FUSE_PARAM(buffer_p,rdev);
    SAVE_FUSE_PARAM(buffer_p,trc_idx);
    
    START_PROFILING_NB(buffer_p,rozofs_ll_mknod);
    
    DEBUG("mknod (%lu,%s,%04o,%08lX)\n", (unsigned long int) parent, name,
            (unsigned int) mode, (unsigned long int) rdev);

    if (strlen(name) > ROZOFS_FILENAME_MAX) {
        errno = ENAMETOOLONG;
        goto error;
    }
    if (!(ie = get_ientry_by_inode(parent))) {
        errno = ENOENT;
        goto error;
    }
    /*
    ** fill up the structure that will be used for creating the xdr message
    */    
    arg.arg_gw.eid = exportclt.eid;
    memcpy(arg.arg_gw.parent,ie->fid, sizeof (uuid_t));
    arg.arg_gw.name = (char*)name;    
    arg.arg_gw.uid  = ctx->uid;
    arg.arg_gw.gid  = ctx->gid;
    arg.arg_gw.mode = mode;
    arg.hdr.gateway_rank = rozofs_get_site_number();     
    /*
    ** now initiates the transaction towards the remote end
    */
#if 1
    ret = rozofs_expgateway_send_routing_common(arg.arg_gw.eid,ie->fid,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_MKNOD,(xdrproc_t) xdr_epgw_mknod_arg_t,(void *)&arg,
                              rozofs_ll_mknod_cbk,buffer_p); 
#else
    ret = rozofs_export_send_common(&exportclt,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_MKNOD,(xdrproc_t) xdr_epgw_mknod_arg_t,(void *)&arg,
                              rozofs_ll_mknod_cbk,buffer_p); 
#endif
    if (ret < 0) goto error;
    

    /*
    ** no error just waiting for the answer
    */


    return;
error:
    rozofs_trc_rsp(srv_rozofs_ll_mknod,parent,NULL,1,trc_idx);
    fuse_reply_err(req, errno);
    STOP_PROFILING_NB(buffer_p,rozofs_ll_mknod);
    if (buffer_p != NULL) rozofs_fuse_release_saved_context(buffer_p);
    return;
}
        
/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated rozofs_fuse_context
 
 @return none
 */
void rozofs_ll_mknod_cbk(void *this,void *param)
{

   struct fuse_entry_param fep;
   ientry_t *nie = 0;
   ientry_t *pie = 0;
   struct stat stbuf;
   fuse_req_t req; 
   epgw_mattr_ret_t ret ;
   struct rpc_msg  rpc_reply;
   xdrproc_t decode_proc = (xdrproc_t)xdr_epgw_mattr_ret_t;

   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   mattr_t  attrs;
   mattr_t  pattrs;
   rozofs_fuse_save_ctx_t *fuse_ctx_p;
   errno = 0;
   int trc_idx;
    
   GET_FUSE_CTX_P(fuse_ctx_p,param);    
   
   rpc_reply.acpted_rply.ar_results.proc = NULL;
   RESTORE_FUSE_PARAM(param,req);
   RESTORE_FUSE_PARAM(param,trc_idx);
    /*
    ** get the pointer to the transaction context:
    ** it is required to get the information related to the receive buffer
    */
    rozofs_tx_ctx_t  *rozofs_tx_ctx_p = (rozofs_tx_ctx_t*)this;     
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
    ** ok now call the procedure to encode the message
    */
    memset(&ret,0, sizeof(ret));                            
    if (decode_proc(&xdrs,&ret) == FALSE)
    {
       TX_STATS(ROZOFS_TX_DECODING_ERROR);
       errno = EPROTO;
       xdr_free((xdrproc_t) decode_proc, (char *) &ret);
       goto error;
    }   
    /*
    **  This gateway do not support the required eid 
    */    
    if (ret.status_gw.status == EP_FAILURE_EID_NOT_SUPPORTED) {    

        /*
        ** Do not try to select this server again for the eid
        ** but directly send to the exportd
        */
        expgw_routing_expgw_for_eid(&fuse_ctx_p->expgw_routing_ctx, ret.hdr.eid, EXPGW_DOES_NOT_SUPPORT_EID);       

        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    

        /* 
        ** Attempt to re-send the request to the exportd and wait being
        ** called back again. One will use the same buffer, just changing
        ** the xid.
        */
        status = rozofs_expgateway_resend_routing_common(rozofs_tx_ctx_p, NULL,param); 
        if (status == 0)
        {
          /*
          ** do not forget to release the received buffer
          */
          ruc_buf_freeBuffer(recv_buf);
          recv_buf = NULL;
          return;
        }           
        /*
        ** Not able to resend the request
        */
        errno = EPROTO; /* What else ? */
        goto error;
         
    }

    if (ret.status_gw.status == EP_FAILURE) {
        errno = ret.status_gw.ep_mattr_ret_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        goto error;
    }
    
    /*
    ** Update eid free quota
    */
    eid_set_free_quota(ret.free_quota);  
        
    memcpy(&attrs, &ret.status_gw.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    /*
    ** get the parent attributes
    */
    memcpy(&pattrs, &ret.parent_attr.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));

    xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
    /*
    ** end of decoding
    */
    if (!(nie = get_ientry_by_fid(attrs.fid))) {
        nie = alloc_ientry(attrs.fid);
    }

    memset(&fep, 0, sizeof (fep));
    fep.ino = nie->inode;
    mattr_to_stat(&attrs, &stbuf,exportclt.bsize);
    stbuf.st_ino = nie->inode;
    /*
    ** update the attributes in the ientry
    */
    memcpy(&nie->attrs,&attrs, sizeof (mattr_t));
    /**
    *  update the timestamp in the ientry context
    */
    nie->timestamp = rozofs_get_ticker_us();
    /*
    ** get the parent attributes
    */
    pie = get_ientry_by_fid(pattrs.fid);
    if (pie != NULL)
    {
      memcpy(&pie->attrs,&pattrs, sizeof (mattr_t));
      /**
      *  update the timestamp in the ientry context
      */
      pie->timestamp = rozofs_get_ticker_us();
    }    
    /*
    ** check the length of the file, and update the ientry if the file size returned
    ** by the export is greater than the one found in ientry
    */
    if (nie->attrs.size < stbuf.st_size) nie->attrs.size = stbuf.st_size;
    stbuf.st_size = nie->attrs.size;
        
    fep.attr_timeout = rozofs_tmr_get(TMR_FUSE_ATTR_CACHE);
    fep.entry_timeout = rozofs_tmr_get(TMR_FUSE_ENTRY_CACHE);
    memcpy(&fep.attr, &stbuf, sizeof (struct stat));
    nie->nlookup++;
    fuse_reply_entry(req, &fep);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    /*
    ** release the transaction context and the fuse context
    */
    rozofs_trc_rsp(srv_rozofs_ll_mknod,(nie==NULL)?0:nie->inode,(nie==NULL)?NULL:nie->attrs.fid,status,trc_idx);

    STOP_PROFILING_NB(param,rozofs_ll_mknod);
    rozofs_fuse_release_saved_context(param);
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);        
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
    return;
}
