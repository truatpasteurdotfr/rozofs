
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
#include "config.h"
#include "file.h"
#include "rozofs_fuse.h"
#include "rozofs_fuse_api.h"
#include "rozofsmount.h"
#include <rozofs/core/rozofs_tx_common.h>
#include <rozofs/core/rozofs_tx_api.h>

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
    ep_mfile_arg_t arg;
    int ret;


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
    ** fill up the structure that will be used for creating the xdr message
    */    
    arg.eid = exportclt.eid;
    memcpy(arg.fid, ie->fid, sizeof (uuid_t));
    /*
    ** now initiates the transaction towards the remote end
    */
    ret = rozofs_export_send_common(&exportclt,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_GETATTR,(xdrproc_t) xdr_ep_mfile_arg_t,(void *)&arg,
                              rozofs_ll_getattr_cbk,buffer_p); 
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
   ep_mattr_ret_t ret ;
   int status;
   xdrproc_t decode_proc = (xdrproc_t)xdr_ep_mattr_ret_t;
   
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   mattr_t  attr;
   struct rpc_msg  rpc_reply;
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
    if (ret.status == EP_FAILURE) {
        errno = ret.ep_mattr_ret_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        goto error;
    }
    memcpy(&attr, &ret.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
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

    fuse_reply_attr(req, &stbuf, attr_cache_timeo);
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


void rozofs_ll_setattr_nb(fuse_req_t req, fuse_ino_t ino, struct stat *stbuf,
        int to_set, struct fuse_file_info *fi) 
{
    ientry_t *ie = 0;
    mattr_t attr;
    ep_setattr_arg_t arg;
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
    stat_to_mattr(stbuf, &attr,to_set);
    /*
    ** set the argument to encode
    */
    arg.eid = exportclt.eid;
    memcpy(&arg.attrs, &attr, sizeof (mattr_t));
    memcpy(arg.attrs.fid, ie->fid, sizeof (fid_t));
    arg.to_set = to_set;
    /*
    ** now initiates the transaction towards the remote end
    */
    ret = rozofs_export_send_common(&exportclt,EXPORT_PROGRAM, EXPORT_VERSION,
                              EP_SETATTR,(xdrproc_t) xdr_ep_setattr_arg_t,(void *)&arg,
                              rozofs_ll_setattr_cbk,buffer_p); 
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
    struct stat o_stbuf;
    fuse_req_t req; 
    ep_mattr_ret_t ret ;
    mattr_t  attr;

    int status;
    uint8_t  *payload;
    void     *recv_buf = NULL;   
    XDR       xdrs;    
    int      bufsize;
   struct rpc_msg  rpc_reply;
   xdrproc_t decode_proc = (xdrproc_t)xdr_ep_mattr_ret_t;

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
    if (ret.status == EP_FAILURE) {
        errno = ret.ep_mattr_ret_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        goto error;
    }
    memcpy(&attr, &ret.ep_mattr_ret_t_u.attrs, sizeof (mattr_t));
    xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
    /*
    ** end of the the decoding part
    */

    mattr_to_stat(&attr, &o_stbuf);
    o_stbuf.st_ino = ino;
    fuse_reply_attr(req, &o_stbuf, attr_cache_timeo);

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


