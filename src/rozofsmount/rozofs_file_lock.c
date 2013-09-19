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
#include <inttypes.h>
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
#include <rozofs/core/expgw_common.h>


extern uint64_t   rozofs_client_hash;
static ruc_obj_desc_t pending_lock_list;  
static int loop_count=0; 

int rozofs_ll_setlk_internal(file_t * file);
void rozofs_ll_setlk_internal_cbk(void *this,void * param);

DECLARE_PROFILING(mpp_profiler_t);



typedef struct _LOCK_STATISTICS_T {
  uint64_t      bsd_set_passing_lock;
  uint64_t      bsd_set_blocking_lock;
  uint64_t      posix_set_passing_lock;
  uint64_t      posix_set_blocking_lock;
  uint64_t      posix_get_lock;
  uint64_t      set_lock_refused;
  uint64_t      set_lock_success;  
  uint64_t      set_lock_error;
  uint64_t      set_lock_interrupted;  
  uint64_t      get_lock_refused;  
  uint64_t      get_lock_success; 
  uint64_t      get_lock_error;   
  uint64_t      enoent;
  uint64_t      enomem;  
  uint64_t      einval;
  uint64_t      send_common;
} LOCK_STATISTICS_T;

static LOCK_STATISTICS_T lock_stat;

/*
**____________________________________________________
* reset lock statistics
*
*/
void reset_lock_stat(void) {
  memset((char*)&lock_stat,0,sizeof(lock_stat));
}

/*
**____________________________________________________
* Display lock statistics
*
*/
#define DISPLAY_LOCK_STAT(name) p += sprintf(p,"%-24s = %"PRIu64"\n",#name,lock_stat.name);
char * display_lock_stat(char * p) {
  DISPLAY_LOCK_STAT(bsd_set_passing_lock);
  DISPLAY_LOCK_STAT(bsd_set_blocking_lock);  
  DISPLAY_LOCK_STAT(posix_set_passing_lock);    
  DISPLAY_LOCK_STAT(posix_set_blocking_lock);
  DISPLAY_LOCK_STAT(set_lock_refused);
  DISPLAY_LOCK_STAT(set_lock_success);
  DISPLAY_LOCK_STAT(set_lock_error);
  DISPLAY_LOCK_STAT(set_lock_interrupted);
  DISPLAY_LOCK_STAT(posix_get_lock);  
  DISPLAY_LOCK_STAT(get_lock_refused);
  DISPLAY_LOCK_STAT(get_lock_success);
  DISPLAY_LOCK_STAT(get_lock_error);
  DISPLAY_LOCK_STAT(enoent);
  DISPLAY_LOCK_STAT(enomem);
  DISPLAY_LOCK_STAT(einval);
  DISPLAY_LOCK_STAT(send_common);
  return p;
}
/**
**____________________________________________________
*  Compute flocks start and stop
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated rozofs_fuse_context
 
 @return none
 */
ep_lock_size_t rozofs_flock_canonical(struct flock * lock, file_t * f, int64_t * start, int64_t * stop) {

  *stop = 0;
  
  if  (lock->l_whence == SEEK_CUR) {
    *start = f->current_pos;
  }
  else if (lock->l_whence == SEEK_END)  {
    *start = f->attrs.size;
  }  
  else  {
    *start = 0;
  }
  *start += lock->l_start;
  if (*start < 0) return EP_LOCK_NULL;

  if (lock->l_len < 0) {
    *stop = *start;
    *start += lock->l_len;
    if (*start < 0) return EP_LOCK_NULL;
  }
  else if (lock->l_len > 0) {
    *stop = *start + lock->l_len;
  }
  if (*stop == 0) {
    if (*start == 0) return EP_LOCK_TOTAL;
    return EP_LOCK_TO_END;
  }
  if (*start == 0) return EP_LOCK_FROM_START;
  return EP_LOCK_PARTIAL;
}
/**
**____________________________________________________
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated rozofs_fuse_context
 
 @return none
 */

void rozofs_poll_cbk(void *this,void *param) 
{
   void     *recv_buf = NULL;   
   int       status;

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


error:
  
    /*
    ** release the transaction context and the fuse context
    */
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);        
    return;
}
/*
**____________________________________________________
*/
/*
  Periodicaly send a pool file lock to the export
*/
char * rozofs_flock_service_display_lock_list(char * pChar) {
  file_t            * file;
  ruc_obj_desc_t    * link;
 
  /*
  ** Re attemp a file lock if any is pending 
  */
  link = ruc_objGetFirst(&pending_lock_list);
  if (link == NULL) {    
    pChar += sprintf(pChar,"no pending lock request\n");
    return pChar;
  }   
  
  file = (file_t *) ruc_listGetAssoc(link);
  pChar += sprintf(pChar,"1rst pending lock request:\n");
  pChar += sprintf(pChar,"  type  = %s\n", (file->lock_type==EP_LOCK_WRITE)?"WRITE":"READ");
  pChar += sprintf(pChar,"  owner = %llx\n", (long long unsigned int)file->lock_owner_ref);
  return pChar;
}  

/*
**____________________________________________________
*/
/*
  Periodicaly send a pool file lock to the export
*/
void rozofs_flock_service_periodic(void * ns) {
  epgw_lock_arg_t     arg;
  file_t            * file;
  ruc_obj_desc_t    * link;
  int                 nbTxCredits;
 
  loop_count++;  
  
  /*
  ** Check there is some Tx credits to start a transaction
  */
  nbTxCredits = rozofs_tx_get_free_ctx_number() - rozofs_fuse_get_free_ctx_number();
  if (nbTxCredits <= 0) return;
  
  /* Get one credit for the polling */
  nbTxCredits--;
  
  /* No more than 3 requests at a time */
  if (nbTxCredits > 3) nbTxCredits = 3;
  /*
  ** Re attemp a file lock if any is pending 
  */
  while (nbTxCredits) {

    nbTxCredits--;
     
    link = ruc_objGetFirst(&pending_lock_list);
    if (link == NULL) break;  

    ruc_objRemove(link);
    file = (file_t *) ruc_listGetAssoc(link);

    /*
    ** Check that this entry is still valid
    */
    if ((file->chekWord != FILE_CHECK_WORD)
    ||  (file->fuse_req == NULL)
    ||  (file->lock_owner_ref == 0)) {
      lock_stat.set_lock_interrupted++;    
      continue;
    }
    if (rozofs_ll_setlk_internal(file) != 0) {  
      /* If lock request can not be sent, rechain the lock request immediatly */
      ruc_objInsertTail(&pending_lock_list,link);
    }
  }
  
  /* We have previously booked one credit to send the poll */
  if (loop_count < 300) return;
  
  /*
  ** Send the periodic poll message to the export in order to keep
  ** the lock active
  */
  loop_count = 0;
  arg.arg_gw.eid             = exportclt.eid;
  arg.arg_gw.lock.client_ref = rozofs_client_hash;
  /*
  ** now initiates the transaction towards the remote end
  */
  rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),EXPORT_PROGRAM, EXPORT_VERSION,
                            EP_POLL_FILE_LOCK,(xdrproc_t) xdr_epgw_lock_arg_t,(void *)&arg, 
			    rozofs_poll_cbk,NULL); 
}

/**
*  Initialize the file lock service
 
*/
void rozofs_flock_service_init(void) {
  struct timer_cell * periodic_timer;

  reset_lock_stat();  
  
  /*
  ** Initialize the head of list of file waiting for a blocking lock 
  */ 
  ruc_listHdrInit(&pending_lock_list);

  /*
  ** Start a periodic timer every 100 ms
  */
  periodic_timer = ruc_timer_alloc(0,0);
  if (periodic_timer == NULL) {
    severe("no timer");
    return;
  }
  ruc_periodic_timer_start (periodic_timer, 100,rozofs_flock_service_periodic,0);
}
/**
*  File lock testing

 Under normal condition the service ends by calling : fuse_reply_entry
 Under error condition it calls : fuse_reply_err

 @param req: pointer to the fuse request context (must be preserved for the transaction duration
 @param parent : inode parent provided by rozofsmount
 @param name : name to search in the parent directory
 
 @retval none
*/
void rozofs_ll_getlk_cbk(void *this,void *param);

void rozofs_ll_getlk_nb(fuse_req_t req, 
                        fuse_ino_t ino, 
                        struct fuse_file_info *fi,
                        struct flock *flock) {
    ientry_t *ie = 0;
    epgw_lock_arg_t arg;
    int    ret;        
    void *buffer_p = NULL;
    int64_t start,stop;
    file_t      * file;

    lock_stat.posix_get_lock++;
        
    /*
    ** allocate a context for saving the fuse parameters
    */
    buffer_p = rozofs_fuse_alloc_saved_context();
    if (buffer_p == NULL)
    {
      errno = ENOMEM;
      lock_stat.enomem++;      
      goto error;
    }
    SAVE_FUSE_PARAM(buffer_p,req);
    SAVE_FUSE_PARAM(buffer_p,ino);
    SAVE_FUSE_STRUCT(buffer_p,fi,sizeof(struct fuse_file_info));    
    SAVE_FUSE_STRUCT(buffer_p,flock,sizeof(struct flock));    
    
    START_PROFILING_NB(buffer_p,rozofs_ll_getlk);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
	lock_stat.enoent++;
        goto error;
    }
    
    file = (file_t *) (unsigned long) fi->fh;
    
    /*
    ** fill up the structure that will be used for creating the xdr message
    */    
    arg.arg_gw.eid = exportclt.eid;
    memcpy(arg.arg_gw.fid, ie->fid, sizeof (uuid_t));
    switch(flock->l_type) {
      case F_RDLCK:
        arg.arg_gw.lock.mode = EP_LOCK_READ;
	break;
      case F_WRLCK:
        arg.arg_gw.lock.mode = EP_LOCK_WRITE;
	break;	
      case F_UNLCK:
        arg.arg_gw.lock.mode = EP_LOCK_FREE;
        break;
      default:
        errno= EINVAL;
	lock_stat.einval++;
        goto error;
    }
    arg.arg_gw.lock.client_ref   = rozofs_client_hash;
    arg.arg_gw.lock.owner_ref    = fi->lock_owner;
    arg.arg_gw.lock.size         = rozofs_flock_canonical(flock,file, &start, &stop);
    if (arg.arg_gw.lock.size == EP_LOCK_NULL) {
      lock_stat.einval++;
      errno= EINVAL;
      goto error;
    }	
    arg.arg_gw.lock.offset_start = start;
    arg.arg_gw.lock.offset_stop  = stop;      			
    /*
    ** now initiates the transaction towards the remote end
    */
    ret = rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),EXPORT_PROGRAM, EXPORT_VERSION,
                                    EP_GET_FILE_LOCK,(xdrproc_t) xdr_epgw_lock_arg_t,(void *)&arg, 
			            rozofs_ll_getlk_cbk,buffer_p); 
			      
    if (ret < 0) {
      lock_stat.send_common++;    
      goto error;
    }
    /*
    ** no error just waiting for the answer
    */
    return;

error:
    fuse_reply_err(req, errno);
    /*
    ** release the buffer if has been allocated
    */
    STOP_PROFILING_NB(buffer_p,rozofs_ll_getlk);
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

void rozofs_ll_getlk_cbk(void *this,void *param) 
{
   struct flock * flock;
   fuse_req_t req; 
   epgw_lock_ret_t ret ;
   struct rpc_msg  rpc_reply;   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc = (xdrproc_t)xdr_epgw_lock_ret_t;
    
   rpc_reply.acpted_rply.ar_results.proc = NULL;
   RESTORE_FUSE_PARAM(param,req);
   RESTORE_FUSE_STRUCT_PTR(param,flock);
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
        
    if (ret.gw_status.status == EP_SUCCESS) {
      flock->l_type = F_UNLCK; 
      lock_stat.get_lock_success++;       
    }        
    else if (ret.gw_status.status == EP_EAGAIN) {
      switch (ret.gw_status.ep_lock_ret_t_u.lock.mode) {
	case EP_LOCK_READ: flock->l_type = F_RDLCK; break;
	case EP_LOCK_WRITE: flock->l_type = F_WRLCK; break;
        default: flock->l_type = F_UNLCK;
      }
      flock->l_whence = SEEK_SET;
      flock->l_start  = 0;
      flock->l_len    = 0;	
      flock->l_pid = ret.gw_status.ep_lock_ret_t_u.lock.owner_ref;
      lock_stat.get_lock_refused++;       
    }
    else {  
      errno = ret.gw_status.ep_lock_ret_t_u.error;
      xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
      goto error;
    }     
 
    xdr_free((xdrproc_t) decode_proc, (char *) &ret);    
    fuse_reply_lock(req, flock);
    goto out;
error:
    lock_stat.get_lock_error++;
    fuse_reply_err(req, errno);
out:
    /*
    ** release the transaction context and the fuse context
    */
    STOP_PROFILING_NB(param,rozofs_ll_getlk);
    rozofs_fuse_release_saved_context(param);
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
    
    return;
}
/**
*  BSD flock() File lock setting

 Under normal condition the service ends by calling : fuse_reply_entry
 Under error condition it calls : fuse_reply_err

 @param req: pointer to the fuse request context (must be preserved for the transaction duration
 @param parent : inode parent provided by rozofsmount
 @param name : name to search in the parent directory
 
 @retval none
*/
void rozofs_ll_flock_nb(fuse_req_t req, 
                              fuse_ino_t ino,
		              struct fuse_file_info *fi, 
		              int op) {
    int          sleep ;
    struct flock flock;


    
    /*
    ** Blocking or not blocking ? 
    */
    if ((op & LOCK_NB) == LOCK_NB) {
      sleep = 0;
      op &= ~LOCK_NB;
      lock_stat.bsd_set_passing_lock++;
    }  
    else {
      sleep = 1;
      lock_stat.bsd_set_blocking_lock++;      
    }

    /*
    ** Translate operation
    */
    switch(op) {
      case LOCK_SH:
	flock.l_type = F_RDLCK;
	break;
      case LOCK_EX:
        flock.l_type = F_WRLCK;
	break;	
      case LOCK_UN:
        flock.l_type = LOCK_UN;
        break;
      default:
	lock_stat.einval++;      
        fuse_reply_err(req, EINVAL);
        return;
    }
    flock.l_whence = SEEK_SET;
    flock.l_start  = 0;
    flock.l_len    = 0;
    flock.l_pid    = fi->lock_owner; 
    
    if (sleep) lock_stat.posix_set_blocking_lock--;   
    else       lock_stat.posix_set_passing_lock--;    
    rozofs_ll_setlk_nb( req, ino, fi, &flock, sleep);

}
/**
*  File lock setting

 Under normal condition the service ends by calling : fuse_reply_entry
 Under error condition it calls : fuse_reply_err

 @param req: pointer to the fuse request context (must be preserved for the transaction duration
 @param parent : inode parent provided by rozofsmount
 @param name : name to search in the parent directory
 
 @retval none
*/
void rozofs_ll_setlk_cbk(void *this,void *param);

void rozofs_ll_setlk_nb(fuse_req_t req, 
                        fuse_ino_t ino, 
                        struct fuse_file_info *fi,
                        struct flock *flock,
			int sleep) {
    ientry_t *ie = 0;
    epgw_lock_arg_t arg;
    int    ret;        
    void *buffer_p = NULL;
    file_t      * file;
    int64_t start,stop;

    if (sleep) lock_stat.posix_set_blocking_lock++;   
    else       lock_stat.posix_set_passing_lock++; 


    /*
    ** allocate a context for saving the fuse parameters
    */
    buffer_p = rozofs_fuse_alloc_saved_context();
    if (buffer_p == NULL)
    {
      lock_stat.enomem++;   
      errno = ENOMEM;
      goto error;
    }
    SAVE_FUSE_PARAM(buffer_p,req);
    SAVE_FUSE_PARAM(buffer_p,sleep);
    SAVE_FUSE_PARAM(buffer_p,ino);
    SAVE_FUSE_STRUCT(buffer_p,fi,sizeof(struct fuse_file_info));    
    SAVE_FUSE_STRUCT(buffer_p,flock,sizeof(struct flock));    
    
    START_PROFILING_NB(buffer_p,rozofs_ll_setlk);

    if (!(ie = get_ientry_by_inode(ino))) {
        errno = ENOENT;
	lock_stat.enoent++;
        goto error;
    }

    file = (file_t *) (unsigned long) fi->fh;
    
    /*
    ** fill up the structure that will be used for creating the xdr message
    */    
    arg.arg_gw.eid = exportclt.eid;
    memcpy(arg.arg_gw.fid, ie->fid, sizeof (uuid_t));
    switch(flock->l_type) {
      case F_RDLCK:
        arg.arg_gw.lock.mode = EP_LOCK_READ;
	break;
      case F_WRLCK:
        arg.arg_gw.lock.mode = EP_LOCK_WRITE;
	break;	
      case F_UNLCK:
        arg.arg_gw.lock.mode = EP_LOCK_FREE;
        break;
      default:
	lock_stat.einval++;      
        errno= EINVAL;
        goto error;
    }
    arg.arg_gw.lock.client_ref   = rozofs_client_hash;
    arg.arg_gw.lock.owner_ref    = fi->lock_owner;
    arg.arg_gw.lock.size         = rozofs_flock_canonical(flock,file, &start, &stop);
    if (arg.arg_gw.lock.size == EP_LOCK_NULL) {
      lock_stat.einval++;    
      errno= EINVAL;
      goto error;
    }	
    arg.arg_gw.lock.offset_start = start;
    arg.arg_gw.lock.offset_stop  = stop; 

    /* Save lock information for next retries when in blocking mode */
    if (sleep) {
      file->lock_size  = arg.arg_gw.lock.size;
      file->lock_start = start;
      file->lock_stop	= stop; 
    }
     	  			
    /*
    ** now initiates the transaction towards the remote end
    */
    ret = rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),EXPORT_PROGRAM, EXPORT_VERSION,
                                    EP_SET_FILE_LOCK,(xdrproc_t) xdr_epgw_lock_arg_t,(void *)&arg, 
			            rozofs_ll_setlk_cbk,buffer_p);     

    if (ret < 0) {
      lock_stat.send_common++;
      goto error;
    }
    /*
    ** no error just waiting for the answer
    */
    return;

error:
    fuse_reply_err(req, errno);
    /*
    ** release the buffer if has been allocated
    */
    STOP_PROFILING_NB(buffer_p,rozofs_ll_setlk);
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

void rozofs_ll_setlk_cbk(void *this,void *param) 
{
   struct flock * flock;
   fuse_req_t req; 
   epgw_lock_ret_t ret ;
   int             sleep;
   struct rpc_msg  rpc_reply;   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc = (xdrproc_t)xdr_epgw_lock_ret_t;
   struct fuse_file_info *fi;
   file_t * file;
    
   rpc_reply.acpted_rply.ar_results.proc = NULL;
   RESTORE_FUSE_PARAM(param,req);
   RESTORE_FUSE_PARAM(param,sleep);
   RESTORE_FUSE_STRUCT_PTR(param,flock);
   RESTORE_FUSE_STRUCT_PTR(param,fi);
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
    
        
    if (ret.gw_status.status == EP_SUCCESS) {
      if (flock->l_type != F_UNLCK) {
        /* 
	** Lock has been set. Save the lock owner in the file structure 
	** to release the lockwhen the file is closed
	*/
        file = (file_t*) (unsigned long) fi->fh;
        file->lock_owner_ref = fi->lock_owner;   
      } 
      errno = 0;      
    }
    else if (ret.gw_status.status == EP_EAGAIN) {
      errno = EAGAIN;
      /*
      ** Case of blocking mode. Put the file structrure in the list of pending
      ** file locks 
      */
      if (sleep) {
        file = (file_t*) (unsigned long) fi->fh;      
        file->fuse_req  = req;
	if (flock->l_type == F_WRLCK) file->lock_type = EP_LOCK_WRITE;
	else                          file->lock_type = EP_LOCK_READ;
        file->lock_owner_ref = fi->lock_owner;   
        ruc_objInsertTail(&pending_lock_list,&file->pending_lock);
	goto out;	
      }
    }
    else {
      errno = ret.gw_status.ep_lock_ret_t_u.error;
    }
    	
    xdr_free((xdrproc_t) decode_proc, (char *) &ret);    

error:
    if (errno == 0)           lock_stat.set_lock_success++;      
    else if (errno == EAGAIN) lock_stat.set_lock_refused++;      
    else                      lock_stat.set_lock_error++;
    fuse_reply_err(req, errno);
    
    /*
    ** release the transaction context and the fuse context
    */
out:    
    STOP_PROFILING_NB(param,rozofs_ll_setlk);
    rozofs_fuse_release_saved_context(param);
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);   
    
    return;
}
/**
*  re attemp a blocking file lock setting

 Under normal condition the service ends by calling : fuse_reply_entry
 Under error condition it calls : fuse_reply_err

 @param req: pointer to the fuse request context (must be preserved for the transaction duration
 @param parent : inode parent provided by rozofsmount
 @param name : name to search in the parent directory
 
 @retval none
*/
int rozofs_ll_setlk_internal(file_t * file) {
    epgw_lock_arg_t arg;
    struct timeval tv;
    int ret;

    gprofiler.rozofs_ll_setlk_int[P_COUNT]++;
    gettimeofday(&tv,(struct timezone *)0);
    file->timeStamp = MICROLONG(tv);

    /*
    ** fill up the structure that will be used for creating the xdr message
    */    
    arg.arg_gw.eid               = exportclt.eid;
    memcpy(arg.arg_gw.fid, file->fid, sizeof (uuid_t));
    arg.arg_gw.lock.mode         = file->lock_type;
    arg.arg_gw.lock.client_ref   = rozofs_client_hash;
    arg.arg_gw.lock.owner_ref    = file->lock_owner_ref;
    arg.arg_gw.lock.size         = file->lock_size;
    arg.arg_gw.lock.offset_start = file->lock_start;
    arg.arg_gw.lock.offset_stop  = file->lock_stop;   
		
    /*
    ** now initiates the transaction towards the remote end
    */
    ret = rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),EXPORT_PROGRAM, EXPORT_VERSION,
                                     EP_SET_FILE_LOCK,(xdrproc_t) xdr_epgw_lock_arg_t,(void *)&arg, 
			             rozofs_ll_setlk_internal_cbk,file); 

   if (ret == 0) return ret;
   gettimeofday(&tv,(struct timezone *)0); 
   gprofiler.rozofs_ll_setlk_int[P_ELAPSE] += (MICROLONG(tv)-file->timeStamp);  
   return ret;   				         
}
/**
*  Call back function call upon a success rpc, timeout or any other rpc failure
*
 @param this : pointer to the transaction context
 @param param: pointer to the associated rozofs_fuse_context
 
 @return none
 */

void rozofs_ll_setlk_internal_cbk(void *this,void * param) 
{
   epgw_lock_ret_t ret ;
   struct rpc_msg  rpc_reply;   
   int status;
   uint8_t  *payload;
   void     *recv_buf = NULL;   
   XDR       xdrs;    
   int      bufsize;
   xdrproc_t decode_proc = (xdrproc_t)xdr_epgw_lock_ret_t;
   file_t * file = (file_t*) param;
   struct timeval tv;

    /*
    ** Check that this entry is still valid
    */
    if ((file->chekWord != FILE_CHECK_WORD)
    ||  (file->fuse_req == NULL)
    ||  (file->lock_owner_ref == 0)) {
      lock_stat.set_lock_interrupted++;        
      goto out;
    }
           
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
    if (status < 0) goto again; 

    /*
    ** get the pointer to the receive buffer payload
    */
    recv_buf = rozofs_tx_get_recvBuf(this);
    if (recv_buf == NULL) goto again;         

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
    
        
    if (ret.gw_status.status == EP_SUCCESS) {
      fuse_reply_err(file->fuse_req, 0);
      file->fuse_req = NULL;      
      xdr_free((xdrproc_t) decode_proc, (char *) &ret);   
      lock_stat.set_lock_success++;         
      goto out;
    }
    
    if (ret.gw_status.status != EP_EAGAIN) {
      errno = ret.gw_status.ep_lock_ret_t_u.error;    	
      xdr_free((xdrproc_t) decode_proc, (char *) &ret); 
      goto error;
    }
    
again:
    /* Rechain the lock request  */
    ruc_objInsertTail(&pending_lock_list,&file->pending_lock);    
    goto out;  
    
error:
    lock_stat.set_lock_error++;
    fuse_reply_err(file->fuse_req, errno);
    file->fuse_req = NULL;
    
out:   
    gettimeofday(&tv,(struct timezone *)0); 
    gprofiler.rozofs_ll_setlk_int[P_ELAPSE] += (MICROLONG(tv)-file->timeStamp);   
     
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);           
}
/**
*  reset all the file locks of a given owner (at file release for instance)
*  This is an internal request that do not trigger any response toward fuse


 @param fid     :FID of thye file on which the locks are to be removed
 @param owner   :reference of the owner of the lock
 
 @retval none
*/
void rozofs_clear_file_lock_owner_cbk(void *this,void *param);

void rozofs_clear_file_lock_owner(file_t * f) {
    epgw_lock_arg_t arg;
    int    ret;        
    void *buffer_p = NULL;
    
    /*
    ** In case a blocking lock request is pending on this file,
    ** stop requesting exportd for the lock
    */
    if (f->fuse_req != NULL) {
    
      /* 
      ** Reply to fuse 
      */
      fuse_reply_err(f->fuse_req, EINTR);
      f->fuse_req = NULL;
 
      /* 
      ** Eventualy remove it from the list of request waiting for a lock
      */
      ruc_objRemove(&f->pending_lock);
    }
    
    
    
    /*
    ** Now ask the exportd to forget all the locks of this guy
    */
    
    
    /*
    ** allocate a context for saving the fuse parameters
    */
    buffer_p = rozofs_fuse_alloc_saved_context();
    if (buffer_p == NULL)
    {
      lock_stat.enomem++;   
      errno = ENOMEM;
      goto error;
    }  
    
    START_PROFILING_NB(buffer_p,rozofs_ll_clearlkowner);

    /*
    ** fill up the structure that will be used for creating the xdr message
    */    
    arg.arg_gw.eid               = exportclt.eid;
    memcpy(arg.arg_gw.fid, f->fid, sizeof (uuid_t));
    arg.arg_gw.lock.mode         = EP_LOCK_FREE;
    arg.arg_gw.lock.client_ref   = rozofs_client_hash;
    arg.arg_gw.lock.owner_ref    = f->lock_owner_ref;
    arg.arg_gw.lock.size         = EP_LOCK_TOTAL;    
    arg.arg_gw.lock.offset_start = 0;
    arg.arg_gw.lock.offset_stop  = 0;      			
    /*
    ** now initiates the transaction towards the remote end
    */
    ret = rozofs_export_send_common(&exportclt,ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM),EXPORT_PROGRAM, EXPORT_VERSION,
                                    EP_CLEAR_OWNER_FILE_LOCK,(xdrproc_t) xdr_epgw_lock_arg_t,(void *)&arg, 
			            rozofs_clear_file_lock_owner_cbk,buffer_p);       

    if (ret >= 0) return;

error:
    /*
    ** release the buffer if has been allocated
    */
    f->lock_owner_ref = 0;  
    STOP_PROFILING_NB(buffer_p,rozofs_ll_clearlkowner);
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

void rozofs_clear_file_lock_owner_cbk(void *this,void *param) 
{
   void     *recv_buf = NULL;   
   int       status;

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


error:
  
    /*
    ** release the transaction context and the fuse context
    */
    STOP_PROFILING_NB(param,rozofs_ll_clearlkowner);
    rozofs_fuse_release_saved_context(param);
    if (rozofs_tx_ctx_p != NULL) rozofs_tx_free_from_ptr(rozofs_tx_ctx_p);    
    if (recv_buf != NULL) ruc_buf_freeBuffer(recv_buf);    
    
    return;
}
