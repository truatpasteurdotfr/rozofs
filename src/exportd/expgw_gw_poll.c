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
#include <rozofs/common/profile.h>
#include "expgw_gwprotosvc.h"
#include <rozofs/rpc/epproto.h>
#include <rozofs/core/expgw_common.h>


DECLARE_PROFILING(epp_profiler_t);

/*
**______________________________________________________________________________
*/
/**
*  GW PROGRAM: GW_POLL

  
  Upon receiving a GW_POLL from the exportd master, the application
  check if the configuration information of the received message matches
  with the current owned one.
  If there is a match it answer GW_SUCCESS otherwise it answer GW_FAILURE
  
  @param 
*/
void gw_poll_1_nblocking_svc_cbk(gw_header_t * arg, rozorpc_srv_ctx_t *req_ctx_p) 
{
    static gw_status_t ret;    
    uint32_t hash_config;
    
    /**
    *  Get the current hash configuration for the exportd id
    */
    hash_config = expgw_get_exportd_hash_config(arg->export_id);
    if (arg->configuration_indice != hash_config)
    {
      ret.status = GW_NOT_SYNCED;  
    }
    else
    {
      ret.status = GW_SUCCESS;      
    }      
    /*
    ** use the receive buffer for the reply
    */
    req_ctx_p->xmitBuf = req_ctx_p->recv_buf;
    req_ctx_p->recv_buf = NULL;
    rozorpc_srv_forward_reply(req_ctx_p,(char*)&ret);
    /*
    ** release the context
    */
    rozorpc_srv_release_context(req_ctx_p);
 
    return;
}


void gw_null_1_nblocking_svc_cbk(void *arg, rozorpc_srv_ctx_t *req_ctx_p)
{

}

void gw_invalidate_sections_1_nblocking_svc_cbk(gw_invalidate_sections_t *arg, rozorpc_srv_ctx_t *req_ctx_p)
{

}


void gw_invalidate_all_1_nblocking_svc_cbk(gw_header_t *arg, rozorpc_srv_ctx_t *req_ctx_p)
{

}

void gw_configuration_1_nblocking_svc_cbk(gw_configuration_t *arg, rozorpc_srv_ctx_t *req_ctx_p)
{
    static gw_status_t ret;
    int status;
    uint32_t *eid_p;
    int rank;
    int i;

    ret.status = GW_SUCCESS;        
#if 0
    info("EXPGW hash_conf %x gateway_rank %d: exportd id %d  nb_gateways %d  nb_eid %d",
         arg->hdr.configuration_indice,
         arg->hdr.gateway_rank,         
         arg->hdr.export_id ,       
         arg->hdr.nb_gateways ,        
         arg->eid.eid_len); 
#endif         
    /*
    ** move to dirty the old configuration
    */
    expgw_set_exportd_table_dirty(arg->hdr.export_id);
    /*
    ** Insert the configuration
    */
    eid_p = arg->eid.eid_val;
    status = 0;
    while(1)
    {
      for (i = 0; i < arg->eid.eid_len;i++)
      {
        status = expgw_export_add_eid(arg->hdr.export_id,   // exportd id
                                   eid_p[i],                // eid
                                   arg->exportd_host,       // hostname of the Master exportd
                                   0,                       // port
                                   arg->hdr.nb_gateways,    // nb Gateway
                                   arg->hdr.gateway_rank    // gateway rank
                                   );    
        if (status < 0) 
        {
          severe("fail to add eid %d",i);
          ret.status = GW_FAILURE;   
          ret.gw_status_t_u.error = EPROTO;     
          break;
        }    
      }
      if (status < 0) break;
      /*
      ** insert the hosts
      */
      for (rank = 0; rank < arg->hdr.nb_gateways; rank++)
      {
        status = expgw_add_export_gateway(arg->hdr.export_id, 
                                         (char*)arg->gateway_host.gateway_host_val[rank].host,
                                         60000+EXPGW_PORT_ROZOFSMOUNT_IDX,
                                         rank); 
        if (status < 0) 
        {
          severe("fail to add host %s",(char*)arg->gateway_host.gateway_host_val[rank].host);
          ret.status = GW_FAILURE;   
          ret.gw_status_t_u.error = EPROTO;     
          break;
        }           
      }
      /*
      ** OK now clear the remain dirty entries
      */
      expgw_clean_up_exportd_table_dirty(arg->hdr.export_id);
      /*
      ** set the new hash of the configuration file
      */
      expgw_set_exportd_hash_config(arg->hdr.export_id,arg->hdr.configuration_indice);
      break;
    }
    /*
    ** use the receive buffer for the reply
    */
    req_ctx_p->xmitBuf = req_ctx_p->recv_buf;
    req_ctx_p->recv_buf = NULL;
    rozorpc_srv_forward_reply(req_ctx_p,(char*)&ret);
    /*
    ** release the context
    */
    rozorpc_srv_release_context(req_ctx_p);
 
    return;
}

