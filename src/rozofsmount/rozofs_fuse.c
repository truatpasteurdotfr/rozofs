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

#define FUSE_USE_VERSION 26

#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>

#include <stdio.h>
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <fcntl.h> 
#include <sys/un.h>             
#include <errno.h>  
 #include <poll.h>
 #include <rozofs/common/log.h>
 
#include <rozofs/common/types.h>
#include <rozofs/core/ruc_common.h>
#include <rozofs/core/ruc_list.h>
#include <rozofs/core/af_unix_socket_generic.h>
#include <rozofs/core/af_unix_socket_generic_api.h>
#include "rozofs_fuse.h"
#include <rpc/rpc.h>

#if 0
#include "ruc_common.h"
#include "ruc_list.h"
#include "af_unix_socket_generic.h"
#include "af_unix_socket_generic_api.h"
#include "rozofs_fuse.h"
#endif


rozofs_fuse_ctx_t  *rozofs_fuse_ctx_p = NULL;  /**< pointer to the rozofs_fuse saved contexts   */

 /**
 * prototypes
 */
uint32_t rozofs_fuse_rcvReadysock(void * rozofs_fuse_ctx_p,int socketId);
uint32_t rozofs_fuse_rcvMsgsock(void * rozofs_fuse_ctx_p,int socketId);
int rozofs_fuse_session_loop(rozofs_fuse_ctx_t *ctx_p);
uint32_t rozofs_fuse_xmitReadysock(void * rozofs_fuse_ctx_p,int socketId);
uint32_t rozofs_fuse_xmitEvtsock(void * rozofs_fuse_ctx_p,int socketId);


/*
**  Call back function for socket controller
*/
ruc_sockCallBack_t rozofs_fuse_callBack_sock=
  {
     rozofs_fuse_rcvReadysock,
     rozofs_fuse_rcvMsgsock,
     rozofs_fuse_xmitReadysock,
     rozofs_fuse_xmitEvtsock
  };



/**
* rozofs fuse xmit and receive channel callbacks for non blocking case
*/
int rozofs_fuse_kern_chan_receive(struct fuse_chan **chp, char *buf,size_t size);
int rozofs_fuse_kern_chan_send(struct fuse_chan *ch, const struct iovec iov[],size_t count);
void rozofs_fuse_kern_chan_destroy(struct fuse_chan *ch);

struct fuse_chan_ops rozofs_fuse_ch_ops = {
	/**
	 * Hook for receiving a raw request
	 *
	 * @param ch pointer to the channel
	 * @param buf the buffer to store the request in
	 * @param size the size of the buffer
	 * @return the actual size of the raw request, or -1 on error
	 */
	.receive = rozofs_fuse_kern_chan_receive,

	/**
	 * Hook for sending a raw reply
	 *
	 * A return value of -ENOENT means, that the request was
	 * interrupted, and the reply was discarded
	 *
	 * @param ch the channel
	 * @param iov vector of blocks
	 * @param count the number of blocks in vector
	 * @return zero on success, -errno on failure
	 */
     .send= rozofs_fuse_kern_chan_send,

	/**
	 * Destroy the channel
	 *
	 * @param ch the channel
	 */
    .destroy = rozofs_fuse_kern_chan_destroy
};






/*
**__________________________________________________________________________

    F U S E   C H A N N E L   C A L L B A C K S
**__________________________________________________________________________
*/
/*

**__________________________________________________________________________
*/
/**
* internal function that is called from processing a message that 
  has been queued on the /dev/fuse socket. That function is
  inherited from fuse_kern_chan_receive
  
  @param chp : pointer to the channel
  @param  buf: pointer to the buffer where data will be copied
  @param  size : max size of the receive buffer
 
  @retval > 0 : number of byte read
  @retval = 0 : session has been exited
  @retval  < 0 : error
 */
int rozofs_fuse_kern_chan_receive(struct fuse_chan **chp, char *buf,
				  size_t size)
{
	struct fuse_chan *ch = *chp;
	int err;
	ssize_t res;
	struct fuse_session *se = fuse_chan_session(ch);
	assert(se != NULL);

restart:
	res = read(fuse_chan_fd(ch), buf, size);
	err = errno;

	if (fuse_session_exited(se))
		return 0;
	if (res == -1) {
		/* ENOENT means the operation was interrupted, it's safe
		   to restart */
		if (err == ENOENT)
			goto restart;

		if (err == ENODEV) {
                    severe("Exit from rozofsmount required !!!");
                    fuse_session_exit(se);
                    rozofs_exit();
                    return 0;
		}
		/* Errors occurring during normal operation: EINTR (read
		   interrupted), EAGAIN (nonblocking I/O), ENODEV (filesystem
		   umounted) */
		if (err != EINTR && err != EAGAIN)
			perror("fuse: reading device");
		return -err;
	}
#if 0
	if ((size_t) res < sizeof(struct fuse_in_header)) {
		fprintf(stderr, "short read on fuse device\n");
		return -EIO;
	}
#endif
	return res;
}
/*
**__________________________________________________________________________
*/
/**
*  Rozofs_fuse channel send:
*  Since Rozofs operates in non-blocking mode it cannot rely on the 
   default fuse_kern_chan_send() operation of fuse since if there is
   a congestion on the fuse device, the response or notification will
   be lost since the caller release the ressource allocated for sending
   the response once it returns from fuse_kern_chan_send().
   To avoid that issue, rozofs MUST be tracked of the response that has
   not been sent and must save it in some internals buffers.
   
    @param ch: fuse channel (contains the reference of the file descriptor to use
    @param iov: list of the vectors to send
    @param count: number of vectors to send
    
    @retval 0 on success 
    @retval < 0 on error
*/
int rozofs_fuse_kern_chan_send(struct fuse_chan *ch, const struct iovec iov[],
			       size_t count)
{
	if (iov) {
		ssize_t res = writev(fuse_chan_fd(ch), iov, count);
		int err = errno;

		if (res == -1) {
			struct fuse_session *se = fuse_chan_session(ch);

			assert(se != NULL);
            
            if(err == EAGAIN)
            {
              /*
              ** fuse device is congestion, so we store the reply and assert 
              ** the congestion flag in the rozofs_fuse context
              */
              
              return 0;
            
            }
			/* ENOENT means the operation was interrupted */
			if (!fuse_session_exited(se) && err != ENOENT)
				perror("fuse: writing device");
			return -err;
		}
	}
	return 0;
}

/*
**__________________________________________________________________________
*/
/**
*  callback that must be called on channel close
   @param ch the channel
*/
void rozofs_fuse_kern_chan_destroy(struct fuse_chan *ch)
{
	close(fuse_chan_fd(ch));
}


/*
**__________________________________________________________________________

    S O C K E T   C O N T R O L L E R    C A L L B A C K S
**__________________________________________________________________________
*/
/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   receiver ready function: called from socket controller.
   The module is intended to return if the receiver is ready to receive a new message
   and FALSE otherwise

    
  @param rozofs_fuse_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
  @retval : TRUE-> receiver ready
  @retval : FALSE-> receiver not ready
*/

uint32_t rozofs_fuse_rcvReadysock(void * rozofs_fuse_ctx_p,int socketId)
{
    rozofs_fuse_ctx_t  *ctx_p;
    uint32_t            buffer_count;
    
    
    ctx_p = (rozofs_fuse_ctx_t*)rozofs_fuse_ctx_p;
    
    /*
    ** check if the session has been exited
    */
    if (fuse_session_exited(ctx_p->se))
    {
      /*
      ** session is dead, so stop receiving fuse request
      */
      return FALSE;
    }
    /*
    ** There is no specific buffer pool needed for receiving the fuse request
    ** since the fuse library allocates memory to store the incoming request.
    ** The only element that can prevent a fuse request to be processed is the 
    ** amount of transaction context. So the system has to check how many transaction
    ** contexts are remaining in the transaction context buffer pool. 
    ** When there is no enough contexts, then the system stops looking at the 
    ** fuse "socket". 
    */
    buffer_count = ruc_buf_getFreeBufferCount(ctx_p->fuseReqPoolRef);
    if (buffer_count == 0) return FALSE;

    return TRUE;
}
  
/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   Called from the socket controller when there is a message pending on the
   socket associated with the context provide in input arguments.


    
  @param rozofs_fuse_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
   @retval : TRUE-> xmit ready event expected
  @retval : FALSE-> xmit  ready event not expected
*/

uint32_t rozofs_fuse_rcvMsgsock(void * rozofs_fuse_ctx_p,int socketId)
{
    rozofs_fuse_ctx_t  *ctx_p;
    
    ctx_p = (rozofs_fuse_ctx_t*)rozofs_fuse_ctx_p;    
    
    rozofs_fuse_session_loop(ctx_p);
    
    return TRUE;
}




/*
**__________________________________________________________________________
*/
int rozofs_fuse_session_loop(rozofs_fuse_ctx_t *ctx_p)
{
	int res = 0;
    char *buf;
    struct fuse_buf fbuf;
    int exit_req = 0;
    struct fuse_session *se = ctx_p->se;
	struct fuse_chan *ch = fuse_session_next_chan(se, NULL);
    
    /*
    ** Get a buffer from the rozofs_fuse context. That buffer is unique and is allocated
    ** at startup.
    */
    buf = ctx_p->buf_fuse_req_p;
    
	while (1) {
		struct fuse_chan *tmpch = ch;

        /*
        ** set the reference of the buffer that will be used by fuse
        */
        fbuf.mem     = buf;
        fbuf.flags   = 0;
        fbuf.size    = ctx_p->bufsize;
		res = fuse_session_receive_buf(se, &fbuf, &tmpch);
        if (res == 0)
        {
           /*
           ** session has been exited
           */
           exit_req = 1;
           break;                   
        }
        if (res < 0)
        {
          switch(errno)
          {
            case EINTR:
             continue;
            case EAGAIN:
             /*
             ** the fuse queue is empty
             */
             return 0;
             break;   
             default:
             /*
             ** fatal error
             */
             exit_req = 1;
             break;        
          }
        }
        /*
        ** OK it looks like that there is a valid message
        */
        
		if ( exit_req == 0) fuse_session_process_buf(se, &fbuf, tmpch);
        if (fuse_session_exited(se) == 1)
        {
           exit_req = 1;
           break;
        
        }
        break;
	}
/*
** to be reworked
*/
//	free(buf);
//	fuse_session_reset(se);
	return res < 0 ? -1 : 0;

}



/*
**__________________________________________________________________________
*/
/**
  Application callBack:

  Called from the socket controller. The application is intended to
  indicate if it wants to be warn when the transmitter is ready
  Typically after facing a congestion on xmit, the application will
  generally request from xmit ready event from the socket layer


  @param rozofs_fuse_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
  @retval : always TRUE
*/

uint32_t rozofs_fuse_xmitReadysock(void * rozofs_fuse_ctx_p,int socketId)
{
    rozofs_fuse_ctx_t  *ctx_p;
    uint32_t ret = TRUE;
    
    ctx_p = (rozofs_fuse_ctx_t*)rozofs_fuse_ctx_p;
    
    if (ctx_p->congested) ret = FALSE;

    return ret;
}


/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   Called from the socket controller upon receiving a xmit ready event
   for the associated socket. That callback is activeted only if the application
   has replied TRUE in rozofs_fuse_xmitReadysock().
   
   It typically the processing of a end of congestion on the socket

    
  @param rozofs_fuse_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
   @retval :always TRUE
*/

uint32_t rozofs_fuse_xmitEvtsock(void * rozofs_fuse_ctx_p,int socketId)
{
    rozofs_fuse_ctx_t  *ctx_p;

    ctx_p = (rozofs_fuse_ctx_t*)rozofs_fuse_ctx_p;

    /*
    ** active the fsm for end of congestion (xmit ready and credit reload
    */
    ctx_p->congested = 1;
    /*
    ** attempt to xmit the pending message
    */
//#warning put code to restart the transmission

    
    return TRUE;
}



/*
**__________________________________________________________________________
*/
/**
*  Init of the pseudo fuse thread 

  @param ch : initial channel
  @param se : initial session
  @param rozofs_fuse_buffer_count : number of request buffers (corresponds to the number of fuse save context)  
  
  @retval 0 on success
  @retval -1 on error
*/
int rozofs_fuse_init(struct fuse_chan *ch,struct fuse_session *se,int rozofs_fuse_buffer_count)
{
  int status = 0;
  
//   return 0;
   
  int fileflags;
  rozofs_fuse_ctx_p = malloc(sizeof (rozofs_fuse_ctx_t));
  if (rozofs_fuse_ctx_p == NULL) 
  {
    /*
    ** cannot allocate memory for fuse rozofs context
    */
    return -1;
  }
  /*
  ** init of the context
  */
  rozofs_fuse_ctx_p->fuseReqPoolRef = NULL;
  rozofs_fuse_ctx_p->ch             = NULL;
  rozofs_fuse_ctx_p->se             = se;
  rozofs_fuse_ctx_p->bufsize        = 0; 
  rozofs_fuse_ctx_p->buf_fuse_req_p = NULL;
  
  while (1)
  {
     /*
     ** get the receive buffer size for former channel in order to create the request distributor
     */
     int bufsize = fuse_chan_bufsize(ch);
     rozofs_fuse_ctx_p->bufsize = bufsize;
     /*
     ** create the pool
     */
     rozofs_fuse_ctx_p->fuseReqPoolRef = ruc_buf_poolCreate(rozofs_fuse_buffer_count,sizeof(rozofs_fuse_save_ctx_t));
     if (rozofs_fuse_ctx_p->fuseReqPoolRef == NULL)
     {
        ERRLOG "rozofs_fuse_init buffer pool creation error(%d,%d)", (int)rozofs_fuse_buffer_count, (int)sizeof(rozofs_fuse_save_ctx_t) ENDERRLOG ;
        status = -1;
        break;
     }
     /*
     ** allocate a buffer for receiving the fuse request
     */
     rozofs_fuse_ctx_p->buf_fuse_req_p = malloc(bufsize);
     if (rozofs_fuse_ctx_p == NULL) 
     {     
        ERRLOG "rozofs_fuse_init out of memory %d", bufsize ENDERRLOG ;
        status = -1;
        break;     
     }
     /*
     ** get the fd of the channel
     */
     rozofs_fuse_ctx_p->fd = fuse_chan_fd(ch);
     /*
     ** create a new channel with the specific operation for rozofs (non-blocking)
     */  
     rozofs_fuse_ctx_p->ch = fuse_chan_new(&rozofs_fuse_ch_ops,fuse_chan_fd(ch),fuse_chan_bufsize(ch),rozofs_fuse_ctx_p);  
     if (rozofs_fuse_ctx_p->ch == NULL)
     {
        ERRLOG "rozofs_fuse_init fuse_chan_new error"  ENDERRLOG ;
        status = -1;
        break;          
     }
     /*
     ** remove the association between the initial session and channel
     */
     fuse_session_remove_chan(ch);  
     /*
     ** OK, now add the new channel
     */
     fuse_session_add_chan(se,rozofs_fuse_ctx_p->ch );  
     /*
     ** set the channel in non blocking mode
     */
     if((fileflags=fcntl(rozofs_fuse_ctx_p->fd,F_GETFL,0))==-1)
     {
       RUC_WARNING(errno);
       status = -1; 
       break;   
     }

     if((fcntl(rozofs_fuse_ctx_p->fd,F_SETFL,fileflags|O_NDELAY))==-1)
     {
       RUC_WARNING(errno);
       status = -1; 
       break;   
     }
     /*
     ** perform the connection with the socket controller
     */
   /*
   ** OK, we are almost done, just need to connect with the socket controller
   */
   rozofs_fuse_ctx_p->connectionId = ruc_sockctl_connect(rozofs_fuse_ctx_p->fd,  // Reference of the socket
                                              "rozofs_fuse",                 // name of the socket
                                              3,                             // Priority within the socket controller
                                              (void*)rozofs_fuse_ctx_p,      // user param for socketcontroller callback
                                              &rozofs_fuse_callBack_sock);   // Default callbacks
    if (rozofs_fuse_ctx_p->connectionId == NULL)
    {
       /*
       ** Fail to connect with the socket controller
       */
       RUC_WARNING(-1);
       status = -1; 
       break;   
    } 

     status = 0;
     break;
  }
  return status;
  
}


