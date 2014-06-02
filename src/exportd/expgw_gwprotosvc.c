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

#include <stdio.h>
#include <stdlib.h>
#include <rpc/pmap_clnt.h>
#include <string.h>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <rozofs/rpc/gwproto.h>
#include <rozofs/rozofs.h>
#include <rozofs/core/rozofs_rpc_non_blocking_generic_srv.h>
#include <rozofs/core/ruc_buffer_api.h>
#include <rozofs/rpc/rozofs_rpc_util.h>
#include <rozofs/rpc/export_profiler.h>
#include "expgw_gwprotosvc.h"
#include <rozofs/rpc/epproto.h>

/*
**__________________________________________________________________________
*/
/**
  Server callback  for GW_PROGRAM protocol:
    
     GW_INVALIDATE_SECTIONS
     GW_INVALIDATE_ALL
     GW_CONFIGURATION
     GW_POLL
     
  That callback is called upon receiving a GW_PROGRAM message
  from the master exportd

    
  @param socket_ctx_p: pointer to the af unix socket
  @param socketId: reference of the socket (not used)
 
   @retval : TRUE-> xmit ready event expected
  @retval : FALSE-> xmit  ready event not expected
*/
void expgw_exportd_req_rcv_cbk(void *userRef,uint32_t  socket_ctx_idx, void *recv_buf)
{
    uint32_t  *com_hdr_p;
    rozofs_rpc_call_hdr_t   hdr;
    gw_status_t  arg_err;

    rozorpc_srv_ctx_t *rozorpc_srv_ctx_p = NULL;
    
    com_hdr_p  = (uint32_t*) ruc_buf_getPayload(recv_buf); 
    com_hdr_p +=1;   /* skip the size of the rpc message */

    memcpy(&hdr,com_hdr_p,sizeof(rozofs_rpc_call_hdr_t));
    scv_call_hdr_ntoh(&hdr);
    /*
    ** allocate a context for the duration of the transaction since it might be possible
    ** that the gateway needs to interrogate the exportd and thus needs to save the current
    ** request until receiving the response from the exportd
    */
    rozorpc_srv_ctx_p = rozorpc_srv_alloc_context();
    if (rozorpc_srv_ctx_p == NULL)
    {
       fatal(" Out of exportd gateway context");    
    }
    /*
    ** save the initial transaction id, received buffer and reference of the connection
    */
    rozorpc_srv_ctx_p->src_transaction_id = hdr.hdr.xid;
    rozorpc_srv_ctx_p->recv_buf  = recv_buf;
    rozorpc_srv_ctx_p->socketRef = socket_ctx_idx;

	union {
		gw_invalidate_sections_t gw_invalidate_sections_1_arg;
		gw_header_t gw_invalidate_all_1_arg;
		gw_configuration_t gw_configuration_1_arg;
		gw_header_t gw_poll_1_arg;
	} argument;

	xdrproc_t _xdr_argument, _xdr_result;
	char *(*local)(char *, struct svc_req *);

	switch (hdr.proc) {
	case GW_NULL:
		_xdr_argument = (xdrproc_t) xdr_void;
		_xdr_result = (xdrproc_t) xdr_void;
		local = (char *(*)(char *, struct svc_req *)) gw_null_1_nblocking_svc_cbk;
		break;

	case GW_INVALIDATE_SECTIONS:
         START_PROFILING_ROZORPC_SRV(rozorpc_srv_ctx_p,gw_invalidate);
		_xdr_argument = (xdrproc_t) xdr_gw_invalidate_sections_t;
		_xdr_result = (xdrproc_t) xdr_gw_status_t;
		local = (char *(*)(char *, struct svc_req *)) gw_invalidate_sections_1_nblocking_svc_cbk;
		break;

	case GW_INVALIDATE_ALL:
         START_PROFILING_ROZORPC_SRV(rozorpc_srv_ctx_p,gw_invalidate_all);
		_xdr_argument = (xdrproc_t) xdr_gw_header_t;
		_xdr_result = (xdrproc_t) xdr_gw_status_t;
		local = (char *(*)(char *, struct svc_req *)) gw_invalidate_all_1_nblocking_svc_cbk;
		break;

	case GW_CONFIGURATION:
         START_PROFILING_ROZORPC_SRV(rozorpc_srv_ctx_p,gw_configuration);
		_xdr_argument = (xdrproc_t) xdr_gw_configuration_t;
		_xdr_result = (xdrproc_t) xdr_gw_status_t;
		local = (char *(*)(char *, struct svc_req *)) gw_configuration_1_nblocking_svc_cbk;
		break;

	case GW_POLL:
         START_PROFILING_ROZORPC_SRV(rozorpc_srv_ctx_p,gw_poll);
		_xdr_argument = (xdrproc_t) xdr_gw_header_t;
		_xdr_result = (xdrproc_t) xdr_gw_status_t;
		local = (char *(*)(char *, struct svc_req *)) gw_poll_1_nblocking_svc_cbk;
		break;

	default:    
      rozorpc_srv_ctx_p->xmitBuf = rozorpc_srv_ctx_p->recv_buf;
      rozorpc_srv_ctx_p->recv_buf = NULL;
      rozorpc_srv_ctx_p->xdr_result =(xdrproc_t) xdr_gw_status_t;
      arg_err.status = GW_FAILURE;
      arg_err.gw_status_t_u.error = EPROTO;        
      rozorpc_srv_forward_reply(rozorpc_srv_ctx_p,(char*)&arg_err);
      rozorpc_srv_release_context(rozorpc_srv_ctx_p);    
	  return;
	}
	memset((char *)&argument, 0, sizeof (argument));
    /*
    ** save the result encoding/decoding function
    */
    rozorpc_srv_ctx_p->xdr_result = _xdr_result;
    /*
    ** decode the payload of the rpc message
    */
	if (!rozorpc_srv_getargs (recv_buf, (xdrproc_t) _xdr_argument, (caddr_t) &argument)) 
    {
    
        rozorpc_srv_ctx_p->xmitBuf = rozorpc_srv_ctx_p->recv_buf;
        rozorpc_srv_ctx_p->recv_buf = NULL;
        rozorpc_srv_ctx_p->xdr_result = (xdrproc_t)xdr_gw_status_t;
        arg_err.status = GW_FAILURE;
        arg_err.gw_status_t_u.error = errno;        
        rozorpc_srv_forward_reply(rozorpc_srv_ctx_p,(char*)&arg_err);
        /*
        ** release the context
        */
        xdr_free((xdrproc_t)_xdr_argument, (caddr_t) &argument);
        rozorpc_srv_release_context(rozorpc_srv_ctx_p);    
		return;
	}    
    
    /*
    ** call the user call-back
    */
	(*local)((char *)&argument, (void*)rozorpc_srv_ctx_p);
    /*
    ** release any data allocated while decoding
    */
    xdr_free((xdrproc_t)_xdr_argument, (caddr_t) &argument);        
}
