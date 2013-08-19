
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

/* need for crypt */
#define _XOPEN_SOURCE 500
#define FUSE_USE_VERSION 26



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <assert.h>
#include <netinet/tcp.h>

#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/list.h>
#include <rozofs/common/log.h>
#include <rozofs/common/htable.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/sclient.h>
#include <rozofs/rpc/mclient.h>
#include <rozofs/rpc/mpproto.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/storcli_proto.h>
#include <rozofs/rpc/storcli_lbg_prototypes.h>
#include "config.h"
#include "file.h"
#include "rozofs_fuse.h"
#include "rozofs_fuse_api.h"
#include "rozofsmount.h"
#include <rozofs/core/rozofs_tx_common.h>
#include <rozofs/core/rozofs_tx_api.h>
#include <rozofs/core/expgw_common.h>

DECLARE_PROFILING(mpp_profiler_t);



/*
**__________________________________________________________________
*/
/**
 * Get file attributes
 *
 * Valid replies:
 *   fuse_reply_attr
 *   fuse_reply_err
 *
 * @param req request handle
 * @param ino the inode number
 * @param fi for future use, currently always NULL
 */
 void rozofs_ll_getattr_cbk(void *this,void *param); 

 
 void rozofs_ll_getattr_nb(fuse_req_t req, fuse_ino_t ino,
        struct fuse_file_info *fi) 
{
    (void) fi;
    ientry_t *ie = 0;
    epgw_mfile_arg_t arg;
    int ret;
    struct stat stbuf;


    DEBUG("getattr for inode: %lu\n", (unsigned long int) ino);
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
    SAVE_FUSE_PARAM(buffer_p,ino);
    SAVE_FUSE_STRUCT(buffer_p,fi,sizeof( struct fuse_file_info));
    START_PROFILING_NB(buffer_p,rozofs_ll_getattr);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    /*
    ** check the case of the block mode: in that case the attributes are
    ** directly retrieved from the ie entry
    */
    if (rozofs_mode == 1)
    {
      mattr_to_stat(&ie->attrs, &stbuf);
      stbuf.st_ino = ino;   
      fuse_reply_attr(req, &stbuf, rozofs_tmr_get(TMR_FUSE_ATTR_CACHE));
      goto out;   
    }
    /*
    ** fill up the structure that will be used for creating the xdr message
    */    
    arg.arg_gw.eid = exportclt.eid;
    memcpy(arg.arg_gw.fid, ie->fid, sizeof (uuid_t));
    /*
    ** now initiates the transaction towards the remote end
    */
#if 1
    ret = rozofs_expgateway_send_routing_common(arg.arg_gw.eid,ie->fid,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_GETATTR,(xdrproc_t) xdr_epgw_mfile_arg_t,(void *)&arg,
                              rozofs_ll_getattr_cbk,buffer_p); 
    
#else
    ret = rozofs_export_send_common(&exportclt,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_GETATTR,(xdrproc_t) xdr_epgw_mfile_arg_t,(void *)&arg,
                              rozofs_ll_getattr_cbk,buffer_p); 
#endif
    if (ret < 0) goto error;    
    /*
    ** no error just waiting for the answer
    */
    return;

error:
    fuse_reply_err(req, errno);
    /*
    ** release the buffer if has been allocated
    */
out:
    STOP_PROFILING_NB(buffer_p,rozofs_ll_getattr);
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
void rozofs_ll_getattr_cbk(void *this,void *param) 
{
   fuse_ino_t ino;
   struct stat stbuf;
   fuse_req_t req; 
   epgw_mattr_ret_t ret ;
   int status;
   ientry_t *ie = 0;
   xdrproc_t decode_proc = (xdrproc_t)xdr_epgw_mattr_ret_t;
   
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   mattr_t  attr;
   struct rpc_msg  rpc_reply;
   rpc_reply.acpted_rply.ar_results.proc = NULL;
   rozofs_fuse_save_ctx_t *fuse_ctx_p;
    
   GET_FUSE_CTX_P(fuse_ctx_p,param);    
   
   RESTORE_FUSE_PARAM(param,req);
   RESTORE_FUSE_PARAM(param,ino);
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
    bufsize = rozofs_tx_get_small_buffer_size();
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
    memcpy(&attr, &ret.status_gw.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));

    xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
    /*
    ** end of the the decoding part
    */
    /*
    ** store the decoded information in the array that will be
    ** returned to the caller
    */
    mattr_to_stat(&attr, &stbuf);
    stbuf.st_ino = ino;
    /*
    ** get the ientry associated with the fuse_inode
    */

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    /*
    ** copy the attributes in the ientry for the case of the block mode
    */
    memcpy(&ie->attrs,&attr, sizeof (mattr_t));
    /*
    ** check the length of the file, and update the ientry if the file size returned
    ** by the export is greater than the one found in ientry
    */
    if (ie->size < stbuf.st_size) ie->size = stbuf.st_size;
    stbuf.st_size = ie->size;
    
    fuse_reply_attr(req, &stbuf, rozofs_tmr_get(TMR_FUSE_ATTR_CACHE));
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    STOP_PROFILING_NB(param,rozofs_ll_getattr);
    rozofs_fuse_release_saved_context(param);
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
    
    return;
}




/*
**__________________________________________________________________
*/
/**
* Set file attributes
*
* In the 'attr' argument only members indicated by the 'to_set'
* bitmask contain valid values.  Other members contain undefined
* values.
*
* If the setattr was invoked from the ftruncate() system call
* under Linux kernel versions 2.6.15 or later, the fi->fh will
* contain the value set by the open method or will be undefined
* if the open method didn't set any value.  Otherwise (not
* ftruncate call, or kernel version earlier than 2.6.15) the fi
* parameter will be NULL.
*
* Valid replies:
*   fuse_reply_attr
*   fuse_reply_err
*
* @param req request handle
* @param ino the inode number
* @param attr the attributes
* @param to_set bit mask of attributes which should be set
* @param fi file information, or NULL
*
* Changed in version 2.5:
*     file information filled in for ftruncate
*/
void rozofs_ll_setattr_cbk(void *this,void *param); 
void rozofs_ll_truncate_cbk(void *this,void *param); 


void rozofs_ll_setattr_nb(fuse_req_t req, fuse_ino_t ino, struct stat *stbuf,
        int to_set, struct fuse_file_info *fi) 
{
    ientry_t *ie = 0;
    mattr_t attr;
    epgw_setattr_arg_t arg;
    int     ret;
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
    SAVE_FUSE_PARAM(buffer_p,ino);
    SAVE_FUSE_STRUCT(buffer_p,fi,sizeof( struct fuse_file_info));
    START_PROFILING_NB(buffer_p,rozofs_ll_setattr);
    
    DEBUG("setattr for inode: %lu\n", (unsigned long int) ino);


    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    /*
    ** set to attr the attributes that must be set: indicated by to_set
    */
    stat_to_mattr(stbuf, &attr, to_set);
    /*
    ** address the case of the file truncate: update the size of the ientry
    ** when the file is truncated
    */
    if (to_set & FUSE_SET_ATTR_SIZE)
    {
      /*
      ** case of the truncate
      ** start by updating the storaged and then updates the exportd
      ** need to store the setattr attributes in the fuse context
      */

      // Check file size 
      if (attr.size < ROZOFS_FILESIZE_MAX) {
        ie->size = attr.size;
      }else{
        errno = EFBIG;
        goto error;
      } 

      storcli_truncate_arg_t  args;
      int ret;
      int lbg_id;
      uint64_t bid;
      uint16_t last_seg;
      /*
      ** translate the size in a block index for the storaged
      */
      bid = attr.size / ROZOFS_BSIZE;
      last_seg = (attr.size % ROZOFS_BSIZE);

      SAVE_FUSE_PARAM(buffer_p,to_set);
      SAVE_FUSE_STRUCT(buffer_p,stbuf,sizeof(struct stat ));      
      /*
      **  Fill the truncate request:
      **  we take the file information in terms of cid, distribution from the attributes
      **  saved in the ientry
      */
      args.cid = ie->attrs.cid;
      args.layout = exportclt.layout;
      memcpy(args.dist_set, ie->attrs.sids, sizeof (sid_t) * ROZOFS_SAFE_MAX);
      memcpy(args.fid, ie->fid, sizeof (fid_t));
      args.bid      = bid;
      args.last_seg = last_seg;

      lbg_id = storcli_lbg_get_lbg_from_fid(ie->fid);

      /*
      ** now initiates the transaction towards the remote end
      */
      ret = rozofs_storcli_send_common(NULL,ROZOFS_TMR_GET(TMR_STORCLI_PROGRAM),STORCLI_PROGRAM, STORCLI_VERSION,
                                STORCLI_TRUNCATE,(xdrproc_t) xdr_storcli_truncate_arg_t,(void *)&args,
                                rozofs_ll_truncate_cbk,buffer_p,lbg_id); 
      if (ret < 0) goto error;
      /*
      ** all is fine, wait from the response of the storcli and then updates the exportd upon
      ** receiving the answer from storcli
      */
      return;
    }
    /*
    ** set the argument to encode
    */
    arg.arg_gw.eid = exportclt.eid;
    memcpy(&arg.arg_gw.attrs, &attr, sizeof (mattr_t));
    memcpy(arg.arg_gw.attrs.fid, ie->fid, sizeof (fid_t));
    arg.arg_gw.to_set = to_set;
    /*
    ** now initiates the transaction towards the remote end
    */
#if 1
    ret = rozofs_expgateway_send_routing_common(arg.arg_gw.eid,ie->fid,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_SETATTR,(xdrproc_t) xdr_epgw_setattr_arg_t,(void *)&arg,
                              rozofs_ll_setattr_cbk,buffer_p); 

#else
    ret = rozofs_export_send_common(&exportclt,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_SETATTR,(xdrproc_t) xdr_epgw_setattr_arg_t,(void *)&arg,
                              rozofs_ll_setattr_cbk,buffer_p); 
#endif

    if (ret < 0) goto error;
    
    /*
    ** no error just waiting for the answer
    */
    return;    
error:
    fuse_reply_err(req, errno);
    /*
    ** release the buffer if has been allocated
    */
    STOP_PROFILING_NB(buffer_p,rozofs_ll_setattr);
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
void rozofs_ll_setattr_cbk(void *this,void *param) 
{
    fuse_ino_t ino;
    ientry_t *ie = 0;
    struct stat o_stbuf;
    fuse_req_t req; 
    epgw_mattr_ret_t ret ;
    mattr_t  attr;

    int status;
    uint8_t  *payload;
    void     *recv_buf = NULL;   
    XDR       xdrs;    
    int      bufsize;
   struct rpc_msg  rpc_reply;
   xdrproc_t decode_proc = (xdrproc_t)xdr_epgw_mattr_ret_t;
   rozofs_fuse_save_ctx_t *fuse_ctx_p;
    
   GET_FUSE_CTX_P(fuse_ctx_p,param);    

   rpc_reply.acpted_rply.ar_results.proc = NULL;

    RESTORE_FUSE_PARAM(param,req);
    RESTORE_FUSE_PARAM(param,ino);
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
    payload += sizeof(uint32_t); /* skip length */
    /*
    ** OK now decode the received message
    */
    bufsize = rozofs_tx_get_small_buffer_size();
    xdrmem_create(&xdrs,(char*)payload,bufsize,XDR_DECODE);
    /*
    ** decode the rpc part
    */
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
    memcpy(&attr, &ret.status_gw.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
    /*
    ** end of the the decoding part
    */

    mattr_to_stat(&attr, &o_stbuf);
    o_stbuf.st_ino = ino;
    /*
    ** get the ientry associated with the fuse_inode
    */

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    /*
    ** update the attributes in the ientry
    */
    memcpy(&ie->attrs,&attr, sizeof (mattr_t));
    
    /*
    ** check the length of the file, and update the ientry if the file size returned
    ** by the export is greater than the one found in ientry
    */
    if (ie->size < o_stbuf.st_size) ie->size = o_stbuf.st_size;
    o_stbuf.st_size = ie->size;

    fuse_reply_attr(req, &o_stbuf, rozofs_tmr_get(TMR_FUSE_ATTR_CACHE));

    goto out;
error:
    fuse_reply_err(req, errno);
out:
    STOP_PROFILING_NB(param,rozofs_ll_setattr);
    rozofs_fuse_release_saved_context(param);
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);  
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
     return;
}



/*
**__________________________________________________________________
*/
/**
* Truncate  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated rozofs_fuse_context
 
 @return none
 */
void rozofs_ll_truncate_cbk(void *this,void *param) 
{
   struct rpc_msg  rpc_reply;
   struct stat *stbuf;
   fuse_ino_t ino;
   ientry_t *ie = 0;
   fuse_req_t req; 
   mattr_t attr;
   int to_set;
   epgw_setattr_arg_t arg;
       
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   storcli_status_ret_t ret;
   int retcode;
   xdrproc_t decode_proc = (xdrproc_t)xdr_storcli_status_ret_t;

   rpc_reply.acpted_rply.ar_results.proc = NULL;

   RESTORE_FUSE_PARAM(param,req);
   RESTORE_FUSE_PARAM(param,ino);
   RESTORE_FUSE_PARAM(param,to_set);
   RESTORE_FUSE_STRUCT_PTR(param,stbuf);      


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
    ** ok now call the procedure to encode the message
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
    /*
    ** no error, so get the length of the data part
    */
    xdr_free((xdrproc_t) decode_proc, (char *) &ret);
    /*
    ** Keep the fuse context since we need to trigger the update of 
    ** the attributes of the file
    */
    rozofs_tx_free_from_ptr(rozofs_tx_ctx_p); 
    ruc_buf_freeBuffer(recv_buf);  
    /*
    ** Update the exportd with the filesize if that one has changed
    */ 
    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    /*
    ** set to attr the attributes that must be set: indicated by to_set
    */
    stat_to_mattr(stbuf, &attr,to_set);
    /*
    ** set the argument to encode
    */
    arg.arg_gw.eid = exportclt.eid;
    memcpy(&arg.arg_gw.attrs, &attr, sizeof (mattr_t));
    memcpy(arg.arg_gw.attrs.fid, ie->fid, sizeof (fid_t));
    arg.arg_gw.to_set = to_set;
    /*
    ** now initiates the transaction towards the remote end
    */
#if 1
    retcode = rozofs_expgateway_send_routing_common(arg.arg_gw.eid,ie->fid,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_SETATTR,(xdrproc_t) xdr_epgw_setattr_arg_t,(void *)&arg,
                              rozofs_ll_setattr_cbk,param); 

#else
    retcode = rozofs_export_send_common(&exportclt,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_SETATTR,(xdrproc_t) xdr_epgw_setattr_arg_t,(void *)&arg,
                              rozofs_ll_setattr_cbk,param); 
#endif

    if (retcode < 0) goto error;
   
    /*
    ** now wait for the response of the exportd
    */
    return;

error:
    /*
    ** reply to fuse and release the transaction context and the fuse context
    */
    fuse_reply_err(req, errno);
    STOP_PROFILING_NB(param,rozofs_ll_setattr);
    
    rozofs_fuse_release_saved_context(param);
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
    
    return;
}

