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
/*
**   I N C L U D E  F I L E S
*/

#include <stdlib.h>
#include <sys/types.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <rozofs/common/types.h>
#include "export.h"
#include "export_internal_channel.h"
#include <rozofs/rpc/gwproto.h>
#include "export_expgateway_conf.h"
#include "expgw_gwprotosvc.h"


 
/*
**____________________________________________________________
*/
/**
* That procedure is the callback associated with GW_POLL.

 The exportd master checks if the running configuration
 version matches with current exportd configuration
 
 When the configuration does not match (indicated by a GW_FAILURE
 the exportd re-send the configuratio to the export gateway
 
  @param param: pointer to the export gateway context
  @param ret : pointer to the GW_POLL response (NULL in case transmission failure)
  
  @retval none
*/
void gw_poll_1_nblocking_cbk(void *param,void *rpc_ret)
{
   export_expgw_conf_ctx_t *p = (export_expgw_conf_ctx_t *) param;
   gw_status_t  *ret = (gw_status_t*)rpc_ret;
   int status;

   if (ret == NULL)
   {
     /*
     * error on poll: need to wait the next timer expiration
     */
       EXGW_CONF_STATS_NOK(p,poll_counter);
      p->poll_conf_tx_state = EPGW_TX_IDLE;
      return;
   }
   switch (ret->status)
   {
      case GW_FAILURE:
       EXGW_CONF_STATS_NOK(p,poll_counter);
      p->poll_conf_tx_state = EPGW_TX_IDLE;
      return;

      case GW_SUCCESS:
       /*
       ** the configuration is synced
       */
       EXGW_CONF_STATS_OK(p,poll_counter);
       p->conf_state = EPGW_CONF_SYNCED;
       p->poll_conf_tx_state = EPGW_TX_IDLE;     
//       severe("SYNCED");     
       return;


      case GW_NOT_SYNCED:
//       severe("OUT OF SYNC");
       /*
       ** the configuration is no synced
       */
       p->conf_state = EPGW_CONF_NOT_SYNCED;
       /*
       ** set the rank of the export gateway
       */
       EXGW_CONF_STATS_OK(p,poll_counter);
       expgw_conf_local.hdr.gateway_rank = p->index;
       EXGW_CONF_STATS_INC(p,conf_counter);
       status = gw_configuration_1_nblocking(&expgw_conf_local,p->gateway_lbg_id,p);
       if (status < 0)
       {
          /*
          ** return back to the idle poll_conf_tx_state
          */
          EXGW_CONF_STATS_NOK(p,conf_counter);
          p->poll_conf_tx_state = EPGW_TX_IDLE;
          return;  
       }
       /*
       ** OK now wait for the response
       */
       p->poll_conf_tx_state = EPGW_TX_WAIT;
       return; 
       
       default:
//         severe("unexpected status code  %d", ret->status);    
         p->poll_conf_tx_state = EPGW_TX_IDLE;     
         break; 
   }
}



 
/*
**____________________________________________________________
*/
/**
* That procedure is the callback associated with GW_CONFIGURATION.

 The exportd master checks if the running configuration
 version matches with current exportd configuration
 
 When the configuration does not match (indicated by a GW_FAILURE
 the exportd re-send the configuratio to the export gateway
 
  @param param: pointer to the export gateway context
  @param ret : pointer to the GW_POLL response (NULL in case transmission failure)
  
  @retval none
*/
void gw_configuration_1_nblocking_cbk(void *param,void *rpc_ret)
{
   export_expgw_conf_ctx_t *p = (export_expgw_conf_ctx_t *) param;
   gw_status_t  *ret = (gw_status_t*)rpc_ret;

   if (ret == NULL)
   {
     /*
     * error on poll: need to wait the next timer expiration
     */
      EXGW_CONF_STATS_NOK(p,conf_counter);
      p->poll_conf_tx_state = EPGW_TX_IDLE;
      return;
   }
   switch (ret->status)
   {
      case GW_FAILURE:
      EXGW_CONF_STATS_NOK(p,conf_counter);
      p->poll_conf_tx_state = EPGW_TX_IDLE;
      return;

      case GW_SUCCESS:
       /*
       ** the configuration is synced
       */
       EXGW_CONF_STATS_OK(p,conf_counter);
       p->conf_state = EPGW_CONF_SYNCED;
       p->poll_conf_tx_state = EPGW_TX_IDLE;     
//       severe("SYNCED");     
       return;
       
       default:
         EXGW_CONF_STATS_NOK(p,conf_counter);
         p->poll_conf_tx_state = EPGW_TX_IDLE;     
//         severe("unexpected status code  %d", ret->status);   
         break; 
   }
}

/*
**____________________________________________________________
*/
/**
*  that function is called periodically
   The purpose is to check if the export gateway is synced in terms of configuration
   
   The exportd master sends periodically a GW_POLL to the export gateway.
   Upon receiving the answer (see gw_poll_1_nblocking_cbk()) the
   exportd master check if the configuration of the  export gateway 
   is inline with the current one.
   
   When the export gateway is out of sync, the exportd master sends
   to it the current configuration.
   
   @param p: context of the export gateway
   
   @retval none
*/
void export_expgw_check_config( export_expgw_conf_ctx_t *p)
{
   gw_header_t *argp = (gw_header_t *)&expgw_conf_local;
   int ret;
   
   switch (p->poll_conf_tx_state)
   {
     case EPGW_TX_IDLE:
       /*
       ** check if there is a lbg for that entry
       */
       if (p->gateway_lbg_id == -1) return;
       /*
       ** OK, that gateway has a load balancing group to send a GW_POLL to it
       */
       argp->gateway_rank = p->index;
       if ((p->conf_state == EPGW_CONF_UNKNOWN) || (p->conf_state == EPGW_CONF_SYNCED))
       {
         EXGW_CONF_STATS_INC(p,poll_counter);
         ret = gw_poll_1_nblocking((gw_header_t*)argp,p->gateway_lbg_id,p);
         if (ret < 0)
         {
            /*
            ** cannot forward the polling request: wait for the next period
            */
             EXGW_CONF_STATS_NOK(p,poll_counter);
            return;     
         }
       }
       else
       {
         EXGW_CONF_STATS_INC(p,conf_counter);
         ret = gw_configuration_1_nblocking((gw_configuration_t*)argp,p->gateway_lbg_id,p);
         if (ret < 0)
         {
            /*
            ** cannot forward the configuration of the export gateways: wait for the next period
            */
             EXGW_CONF_STATS_NOK(p,conf_counter);
            return;     
         }
       }

       p->poll_conf_tx_state = EPGW_TX_WAIT;
       return;


     default:
     case EPGW_TX_WAIT:
       return;   
   }
}

