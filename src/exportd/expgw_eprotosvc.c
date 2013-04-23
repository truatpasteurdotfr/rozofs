
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
 

#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifndef SIG_PF
#define SIG_PF void(*)(int)
#endif
#include <rozofs/rpc/eproto.h>
#include <rozofs/rozofs.h>
#include <rozofs/core/ruc_buffer_api.h>
#include <rozofs/rpc/rozofs_rpc_util.h>
#include "expgw_export.h"
#include "expgw_proto.h"
#include <rozofs/rpc/epproto.h>

DECLARE_PROFILING(epp_profiler_t);

/**
*  get the arguments of the incoming request: it is mostly a rpc decode

 @param recv_buf : ruc buffer that contains the request
 @param xdr_argument : decoding procedure
 @param argument     : pointer to the array where decoded arguments will be stored
 
 @retval TRUE on success
 @retval FALSE decoding error
*/
int expgw_getargs (void *recv_buf,xdrproc_t xdr_argument, void *argument)
{
   XDR xdrs;
   uint32_t  msg_len;  /* length of the rpc messsage including the header length */
   uint32_t header_len;
   uint8_t  *pmsg;     /* pointer to the first available byte in the application message */
   int      len;       /* effective length of application message               */
   rozofs_rpc_call_hdr_with_sz_t *com_hdr_p;


   /*
   ** Get the full length of the message and adjust it the the length of the applicative part (RPC header+application msg)
   */
   msg_len = ruc_buf_getPayloadLen(recv_buf);
   msg_len -=sizeof(uint32_t);   

   com_hdr_p  = (rozofs_rpc_call_hdr_with_sz_t*) ruc_buf_getPayload(recv_buf);  
   pmsg = rozofs_rpc_set_ptr_on_first_byte_after_rpc_header((char*)&com_hdr_p->hdr,&header_len);
   if (pmsg == NULL)
   {
     errno = EFAULT;
     return FALSE;
   }
   /*
   ** map the memory on the first applicative RPC byte available and prepare to decode:
   ** notice that we will not call XDR_FREE since the application MUST
   ** provide a pointer for storing the file handle
   */
   len = msg_len - header_len;    
   xdrmem_create(&xdrs,(char*)pmsg,len,XDR_DECODE);
   return (*xdr_argument)(&xdrs,argument);
}


/*
**__________________________________________________________________________
*/
/**
  Application callBack:

   THis the callback that is activated upon the recption of an exportd
   request from a rozofsmount client 

    
  @param socket_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
   @retval : TRUE-> xmit ready event expected
  @retval : FALSE-> xmit  ready event not expected
*/
void expgw_rozofs_req_rcv_cbk(void *userRef,uint32_t  socket_ctx_idx, void *recv_buf)
{
    uint32_t  *com_hdr_p;
    rozofs_rpc_call_hdr_t   hdr;
    expgw_ctx_t *expgw_ctx_p = NULL;
    
    com_hdr_p  = (uint32_t*) ruc_buf_getPayload(recv_buf); 
    com_hdr_p +=1;   /* skip the size of the rpc message */

    memcpy(&hdr,com_hdr_p,sizeof(rozofs_rpc_call_hdr_t));
    scv_call_hdr_ntoh(&hdr);
    /*
    ** allocate a context for the duration of the transaction since it might be possible
    ** that the gateway needs to interrogate the exportd and thus needs to save the current
    ** request until receiving the response from the exportd
    */
    expgw_ctx_p = expgw_alloc_context();
    if (expgw_ctx_p == NULL)
    {
       fatal(" Out of exportd gateway context");    
    }
    /*
    ** save the initial transaction id, received buffer and reference of the connection
    */
    expgw_ctx_p->src_transaction_id = hdr.hdr.xid;
    expgw_ctx_p->recv_buf  = recv_buf;
    expgw_ctx_p->socketRef = socket_ctx_idx;
    
	union {
		ep_path_t ep_mount_1_arg;
		uint32_t ep_umount_1_arg;
		uint32_t ep_statfs_1_arg;
		epgw_lookup_arg_t ep_lookup_1_arg;
		epgw_mfile_arg_t ep_getattr_1_arg;
		epgw_setattr_arg_t ep_setattr_1_arg;
		epgw_mfile_arg_t ep_readlink_1_arg;
		epgw_mknod_arg_t ep_mknod_1_arg;
		epgw_mkdir_arg_t ep_mkdir_1_arg;
		epgw_unlink_arg_t ep_unlink_1_arg;
		epgw_rmdir_arg_t ep_rmdir_1_arg;
		epgw_symlink_arg_t ep_symlink_1_arg;
		epgw_rename_arg_t ep_rename_1_arg;
		epgw_readdir_arg_t ep_readdir_1_arg;
		epgw_io_arg_t ep_read_block_1_arg;
		epgw_write_block_arg_t ep_write_block_1_arg;
		epgw_link_arg_t ep_link_1_arg;
		epgw_setxattr_arg_t ep_setxattr_1_arg;
		epgw_getxattr_arg_t ep_getxattr_1_arg;
		epgw_removexattr_arg_t ep_removexattr_1_arg;
		epgw_listxattr_arg_t ep_listxattr_1_arg;
		uint16_t ep_list_cluster_1_arg;
	} argument;
	xdrproc_t _xdr_argument, _xdr_result;
	char *(*local)(char *, struct svc_req *);

	switch (hdr.proc) {
#if 0
	case EP_NULL:
		_xdr_argument = (xdrproc_t) xdr_void;
		_xdr_result = (xdrproc_t) xdr_void;
		local = (char *(*)(char *, struct svc_req *)) ep_null_1_svc;
		break;

	case EP_MOUNT:
		_xdr_argument = (xdrproc_t) xdr_ep_path_t;
		_xdr_result = (xdrproc_t) xdr_ep_mount_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_mount_1_svc;
		break;

	case EP_UMOUNT:
		_xdr_argument = (xdrproc_t) xdr_uint32_t;
		_xdr_result = (xdrproc_t) xdr_epgw_status_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_umount_1_svc;
		break;

	case EP_STATFS:
		_xdr_argument = (xdrproc_t) xdr_uint32_t;
		_xdr_result = (xdrproc_t) xdr_ep_statfs_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_statfs_1_svc;
		break;
#endif
	case EP_LOOKUP:
        START_PROFILING_EXPGW(expgw_ctx_p,ep_lookup);
		_xdr_argument = (xdrproc_t) xdr_epgw_lookup_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_mattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_lookup_1_svc;
		break;

	case EP_GETATTR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_getattr);
		_xdr_argument = (xdrproc_t) xdr_epgw_mfile_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_mattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_getattr_1_svc;
		break;

	case EP_SETATTR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_setattr);
		_xdr_argument = (xdrproc_t) xdr_epgw_setattr_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_mattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_setattr_1_svc;
		break;
#if 0
	case EP_READLINK:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_readlink);
		_xdr_argument = (xdrproc_t) xdr_ep_mfile_arg_t;
		_xdr_result = (xdrproc_t) xdr_ep_readlink_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_readlink_1_svc;
		break;
#endif

	case EP_MKNOD:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_mknod);
		_xdr_argument = (xdrproc_t) xdr_epgw_mknod_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_mattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_mknod_1_svc;
		break;

	case EP_MKDIR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_mkdir);
		_xdr_argument = (xdrproc_t) xdr_epgw_mkdir_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_mattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_mkdir_1_svc;
		break;

	case EP_UNLINK:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_unlink);
		_xdr_argument = (xdrproc_t) xdr_epgw_unlink_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_fid_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_unlink_1_svc;
		break;

	case EP_RMDIR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_rmdir);
		_xdr_argument = (xdrproc_t) xdr_epgw_rmdir_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_fid_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_rmdir_1_svc;
		break;

	case EP_SYMLINK:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_symlink);
		_xdr_argument = (xdrproc_t) xdr_epgw_symlink_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_mattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_symlink_1_svc;
		break;
#if 0
	case EP_RENAME:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_rename);
		_xdr_argument = (xdrproc_t) xdr_ep_rename_arg_t;
		_xdr_result = (xdrproc_t) xdr_ep_fid_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_rename_1_svc;
		break;

	case EP_READDIR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_readdir);
		_xdr_argument = (xdrproc_t) xdr_ep_readdir_arg_t;
		_xdr_result = (xdrproc_t) xdr_ep_readdir_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_readdir_1_svc;
		break;

	case EP_READ_BLOCK:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_read_block);
		_xdr_argument = (xdrproc_t) xdr_ep_io_arg_t;
		_xdr_result = (xdrproc_t) xdr_ep_read_block_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_read_block_1_svc;
		break;
#endif
	case EP_WRITE_BLOCK:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_write_block);
		_xdr_argument = (xdrproc_t) xdr_epgw_write_block_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_io_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_write_block_1_svc;
		break;

	case EP_LINK:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_link);
		_xdr_argument = (xdrproc_t) xdr_epgw_link_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_mattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) expgw_link_1_svc;
		break;
#if 0
	case EP_SETXATTR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_setxattr);
		_xdr_argument = (xdrproc_t) xdr_ep_setxattr_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_status_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_setxattr_1_svc;
		break;

	case EP_GETXATTR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_getxattr);
		_xdr_argument = (xdrproc_t) xdr_ep_getxattr_arg_t;
		_xdr_result = (xdrproc_t) xdr_ep_getxattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_getxattr_1_svc;
		break;

	case EP_REMOVEXATTR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_removexattr);
		_xdr_argument = (xdrproc_t) xdr_ep_removexattr_arg_t;
		_xdr_result = (xdrproc_t) xdr_epgw_status_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_removexattr_1_svc;
		break;

	case EP_LISTXATTR:
         START_PROFILING_EXPGW(expgw_ctx_p,ep_listxattr);
		_xdr_argument = (xdrproc_t) xdr_ep_listxattr_arg_t;
		_xdr_result = (xdrproc_t) xdr_ep_listxattr_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_listxattr_1_svc;
		break;

	case EP_LIST_CLUSTER:
		_xdr_argument = (xdrproc_t) xdr_uint16_t;
		_xdr_result = (xdrproc_t) xdr_ep_cluster_ret_t;
		local = (char *(*)(char *, struct svc_req *)) ep_list_cluster_1_svc;
		break;
#endif
	default:
        expgw_ctx_p->xmitBuf = expgw_ctx_p->recv_buf;
        expgw_ctx_p->recv_buf = NULL;
        errno = EPROTO;
        expgw_reply_error(expgw_ctx_p,errno);		
        expgw_release_context(expgw_ctx_p);    
        return;
	}
	memset ((char *)&argument, 0, sizeof (argument));
    
    /*
    ** save the result encoding/decoding function
    */
    expgw_ctx_p->xdr_result = _xdr_result;
    
	if (!expgw_getargs (recv_buf, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) {
        expgw_ctx_p->xmitBuf = expgw_ctx_p->recv_buf;
        expgw_ctx_p->recv_buf = NULL;
        expgw_reply_error(expgw_ctx_p,errno);
        /*
        ** release the context
        */
        xdr_free((xdrproc_t)_xdr_argument, (caddr_t) &argument);
        expgw_release_context(expgw_ctx_p);    
		return;
	}
    /*
    ** call the user call-back
    */
	(*local)((char *)&argument, (void*)expgw_ctx_p);
    /*
    ** release any data allocated while decoding
    */
     xdr_free((xdrproc_t)_xdr_argument, (caddr_t) &argument);
}
