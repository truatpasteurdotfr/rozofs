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
#include <rozofs/rozofs_timer_conf.h>
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
#include <rozofs/core/rozofs_tx_common.h>
#include <rozofs/core/rozofs_tx_api.h>

#include "config.h"
#include "file.h"
#include "rozofs_fuse.h"
#include "rozofs_fuse_api.h"
#include "rozofsmount.h"

DECLARE_PROFILING(mpp_profiler_t);


void rozofs_ll_read_cbk(void *this,void *param);




/** Reads the distributions on the export server,
 *  adjust the read buffer to read only whole data blocks
 *  and uses the function read_blocks to read data
 *
 * @param *f: pointer to the file structure
 * @param off: offset to read from
 * @param *buf: pointer where the data will be stored: buffer associated with the file_t structure
 * @param len: length to read: (correspond to the max buffer size defined in the exportd parameters
 * @param *last_block_size_p: pointer to store the size of the last block size
 *  read
 *
 * @return: the length read on success, -1 otherwise (errno is set)
 */
 
static int read_buf_nb(void *buffer_p,file_t * f, uint64_t off, char *buf, uint32_t len) 
{
   uint64_t bid = 0;
   uint32_t nb_prj = 0;
   storcli_read_arg_t  args;
   int ret;
   int lbg_id;

   // Nb. of the first block to read
   bid = off / ROZOFS_BSIZE;
   nb_prj = len / ROZOFS_BSIZE;
   if (nb_prj > 32)
   {
     severe("bad nb_prj %d bid %llu off %llu len %u",nb_prj,(long long unsigned int)bid,(long long unsigned int)off,len);   
   }

    // Fill request
    args.cid = f->attrs.cid;
    args.sid = 0; // NS
    args.layout = f->export->layout;
    args.spare = 0; // NS
    memcpy(args.dist_set, f->attrs.sids, sizeof (sid_t) * ROZOFS_SAFE_MAX);
    memcpy(args.fid, f->fid, sizeof (fid_t));
    args.proj_id = 0; // N.S
    args.bid = bid;
    args.nb_proj = nb_prj;

    lbg_id = storcli_lbg_get_lbg_from_fid(f->fid);

    /*
    ** now initiates the transaction towards the remote end
    */
    f->buf_read_pending++;
    ret = rozofs_storcli_send_common(NULL,ROZOFS_TMR_GET(TMR_STORCLI_PROGRAM),STORCLI_PROGRAM, STORCLI_VERSION,
                              STORCLI_READ,(xdrproc_t) xdr_storcli_read_arg_t,(void *)&args,
                              rozofs_ll_read_cbk,buffer_p,lbg_id); 
    if (ret < 0) goto error;
    
    /*
    ** no error just waiting for the answer
    */
    if (f->buf_read_pending > 1)
    {
    
//      severe("FDL Read ahead detected %d", f->buf_read_pending);
    }
    return ret;    
error:
    f->buf_read_pending--;
    return ret;

}

/**
*
*
 @retval 0 : read is done and length_p has the read length
 @retval 1 : read is in progress
*/
int file_read_nb(void *buffer_p,file_t * f, uint64_t off, char **buf, uint32_t len, size_t *length_p) 
{
    int64_t length = -1;
    DEBUG_FUNCTION;
    
    *length_p = -1;
    int ret;


    if ((off < f->read_from) || (off > f->read_pos) ||((off+len) >  f->read_pos ))
    {
       /*
       ** need to read data from disk
       **  1- check if there is some pending data to write
       **  2- trigger a read
       */
       if (f->buf_write_wait)
       {
         /*
         ** send a write request
         */
//         #warning TODO: put code to trigger a write request
         f->buf_write_wait = 0;
         f->write_from = 0; 
         f->write_pos  = 0;         
       }
       /*
       ** check if there is pending read in progress, in such a case, we queue the
       ** current request and we process it later upon the receiving of the response
       */
#if 1
       if (f->buf_read_pending > 0)
       {
          /*
          ** some read are in progress; queue this one
          */
          fuse_ctx_read_pending_queue_insert(f,buffer_p);
          return 1;      
       }
#endif
       ret = read_buf_nb(buffer_p,f,off, f->buffer, f->export->bufsize);
       if (ret < 0)
       {
         *length_p = -1;
          return 0;
       }
       /*
       ** read is in progress
       */
       f->buf_read_wait = 1;
       return 1;
    }     
    /*
    ** data are available in the current buffer
    */    
    length =(len <= (f->read_pos - off )) ? len : (f->read_pos - off);
    *buf = f->buffer + (off - f->read_from);    
    *length_p = (size_t)length;
    return 0;
}




/**
*  data read deferred

 Under normal condition the service ends by calling : fuse_reply_entry
 Under error condition it calls : fuse_reply_err

 @param req: pointer to the fuse request context (must be preserved for the transaction duration
 @param parent : inode parent provided by rozofsmount
 @param name : name to search in the parent directory
 
 @retval none
*/

void rozofs_ll_read_defer(void *param) 
{
   fuse_req_t req; 
   struct fuse_file_info  file_info;
   struct fuse_file_info  *fi = &file_info;
   size_t size;
   uint64_t off;   
   file_t *file;
   int read_in_progress = 0;
   char *buff;
   size_t length = 0;

   while(param)
   {   
   RESTORE_FUSE_PARAM(param,req);
   RESTORE_FUSE_PARAM(param,size);
   RESTORE_FUSE_PARAM(param,off);
   RESTORE_FUSE_STRUCT(param,fi,sizeof( struct fuse_file_info));    

   file = (file_t *) fi->fh;   

    buff = NULL;
    read_in_progress = file_read_nb(param,file, off, &buff, size,&length);
    if (read_in_progress)
    {
      /*
      ** no error just waiting for the answer
      */
      return;    
    }
     /*
     ** The data were available in the current buffer
     */
     if (length == -1)
         goto error;
     fuse_reply_buf(req, (char *) buff, length);
     goto out;           

error:
    fuse_reply_err(req, errno);
    /*
    ** release the buffer if has been allocated
    */
out:
    STOP_PROFILING_NB(param,rozofs_ll_read);
    if (param != NULL) rozofs_fuse_release_saved_context(param);
    /*
    ** Check if there is some other read request that are pending
    */

      param = fuse_ctx_read_pending_queue_get(file);
    }
    return;
}


/**
*  data read

 Under normal condition the service ends by calling : fuse_reply_entry
 Under error condition it calls : fuse_reply_err

 @param req: pointer to the fuse request context (must be preserved for the transaction duration
 @param parent : inode parent provided by rozofsmount
 @param name : name to search in the parent directory
 
 @retval none
*/

void rozofs_ll_read_nb(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off,
        struct fuse_file_info *fi) 
{
    ientry_t *ie = 0;
    void *buffer_p = NULL;
    int read_in_progress = 0;
    char *buff;
    size_t length = 0;
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
    SAVE_FUSE_PARAM(buffer_p,size);
    SAVE_FUSE_PARAM(buffer_p,off);
    SAVE_FUSE_STRUCT(buffer_p,fi,sizeof( struct fuse_file_info));    

    DEBUG("read to inode %lu %llu bytes at position %llu\n",
            (unsigned long int) ino, (unsigned long long int) size,
            (unsigned long long int) off);

    START_PROFILING_IO_NB(buffer_p,rozofs_ll_read, size);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
        goto error;
    }
    file_t *file = (file_t *) (unsigned long) fi->fh;

    buff = NULL;
    read_in_progress = file_read_nb(buffer_p,file, off, &buff, size,&length);
    if (read_in_progress)
    {
      /*
      ** no error just waiting for the answer
      */
      return;    
    }
     /*
     ** The data were available in the current buffer
     */
     if (length == -1)
         goto error;
     fuse_reply_buf(req, (char *) buff, length);
     goto out;           

error:
    fuse_reply_err(req, errno);
    /*
    ** release the buffer if has been allocated
    */
out:
    STOP_PROFILING_NB(buffer_p,rozofs_ll_read);
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
void rozofs_ll_read_cbk(void *this,void *param) 
{
   fuse_req_t req; 
   struct rpc_msg  rpc_reply;
   struct fuse_file_info  file_info;
   struct fuse_file_info  *fi = &file_info;
   size_t size;
   uint64_t off;
   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   storcli_read_ret_no_data_t ret;
   xdrproc_t decode_proc = (xdrproc_t)xdr_storcli_read_ret_no_data_t;
   file_t *file;

   rpc_reply.acpted_rply.ar_results.proc = NULL;
   RESTORE_FUSE_PARAM(param,req);
   RESTORE_FUSE_PARAM(param,size);
   RESTORE_FUSE_PARAM(param,off);
   RESTORE_FUSE_STRUCT(param,fi,sizeof( struct fuse_file_info));    

   file = (file_t *) (unsigned long)  fi->fh;   
   file->buf_read_pending--;
   if (file->buf_read_pending < 0)
   {
     severe("buf_read_pending mismatch, %d",file->buf_read_pending);
     file->buf_read_pending = 0;     
   }
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
        errno = ret.storcli_read_ret_no_data_t_u.error;
        xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
        goto error;
    }
    /*
    ** no error, so get the length of the data part
    */
    int received_len = ret.storcli_read_ret_no_data_t_u.len.len;
    xdr_free((xdrproc_t) decode_proc, (char *) &ret); 
    int position = XDR_GETPOS(&xdrs);
    
    /*
    ** check the length
    */
    if (received_len == 0)
    {
      /*
      ** end of file
      */
      errno = 0;
      goto error;   
    }
    /*
    ** set the pointer to the beginning of the data array on the rpc buffer
    */
    uint8_t *src_p = payload+position;
    uint8_t *dst_p = (uint8_t*)file->buffer;
    memcpy(dst_p,src_p,received_len);
    file->buf_read_wait = 0;
    file->read_from = (off/ROZOFS_BSIZE)*ROZOFS_BSIZE;
    file->read_pos  = file->read_from+(uint64_t)received_len;

    size_t length =
            (size <=(file->read_pos - off)) ? size :(file->read_pos - off);
    char *buff = file->buffer + (off - file->read_from);
    
    fuse_reply_buf(req, (char *) buff, length);
    goto out;
error:
    fuse_reply_err(req, errno);
out:
    /*
    ** release the transaction context and the fuse context
    */
    STOP_PROFILING_NB(param,rozofs_ll_read);
    rozofs_fuse_release_saved_context(param);
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
    /*
    ** Check if there is some other read request that are pending
    */
    {
      void *buffer_p = fuse_ctx_read_pending_queue_get(file);
      if (buffer_p  != NULL) return  rozofs_ll_read_defer(buffer_p) ;
    }

    return;
}