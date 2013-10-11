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
 
#ifndef EXPORT_EXPGATEWAY_CONF_H
#define EXPORT_EXPGATEWAY_CONF_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/


#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <uuid/uuid.h>
#include <netdb.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/core/expgw_common.h>

typedef enum 
{
  EPGW_CONF_UNKNOWN = 0,
  EPGW_CONF_NOT_SYNCED ,
  EPGW_CONF_SYNCED,
  EPGW_CONF_MAX,
} export_expgwc_conf_state_e;

typedef enum 
{
  EPGW_TX_IDLE = 0,
  EPGW_TX_WAIT,
  EPGW_TX_MAX,
} export_expgwc_tx_state_e;

typedef enum
{
  EXPGW_STATS_ATTEMPT = 0,
  EXPGW_STATS_SUCCESS,
  EXPGW_STATS_FAILURE,
  EXPGW_STATS_MAX
} export_expgwc_conf_stats_e;

typedef struct export_expgw_conf_stats_t
{
   uint64_t poll_counter[EXPGW_STATS_MAX];
   uint64_t conf_counter[EXPGW_STATS_MAX];
} export_expgw_conf_stats_t;


#define EXGW_CONF_STATS_INC(p,cpt)  \
{\
   p->stats.cpt[EXPGW_STATS_ATTEMPT]++;\
}

#define EXGW_CONF_STATS_OK(p,cpt)  \
{\
   p->stats.cpt[EXPGW_STATS_SUCCESS]++;\
}

#define EXGW_CONF_STATS_NOK(p,cpt)  \
{\
   p->stats.cpt[EXPGW_STATS_FAILURE]++;\
}

typedef struct export_expgw_conf_ctx_t
{
  ruc_obj_desc_t    link;          /**< To be able to chain the  context on any list */
  uint32_t            index;         /**< rank of the gateway in the configuration (must be the index of the context     */
  uint32_t            free;          /**< Is the context free or allocated TRUE/FALSE    */
  /*
  ** specific part
  */
   export_expgwc_tx_state_e  poll_conf_tx_state;    /**< configuration polling state */
   export_expgwc_conf_state_e  conf_state;    /**< configuration  state */
   
   
   uint16_t  port;     /**< tcp port in host format   */
   uint32_t  ipaddr;   /**< IP address in host format */
   int       current_conf_idx;
   char      hostname[ROZOFS_HOSTNAME_MAX];
   int       gateway_lbg_id;        /**< reference of the load balancing group for reaching the configuration port of the gateway */
   export_expgw_conf_stats_t stats;  /**< statistics */
} export_expgw_conf_ctx_t;


extern uint32_t export_nb_gateways;  /**< number of gateways in the configuration */
extern export_expgw_conf_ctx_t export_expgw_conf_table[];



/*__________________________________________________________________________
*/
/**
* init of the configuration gateway 

  @param p : pointer to the context
  @param rank : index of the export gateway
  
  @retval none;
*/
void export_expgw_conf_ctx_init(export_expgw_conf_ctx_t *p,int rank);

/*__________________________________________________________________________
*/
/**
*  Deletion of a context

  @param rank rank of the context to delete

*/
void export_expgw_conf_ctx_delete(int rank);

/*__________________________________________________________________________
*/
/**
*  Create of a context

  @param rank rank of the context to create
  @param hostname : hostname of the export gateway
  @param port : configuration port of the export gateway

*/
int export_expgw_conf_ctx_create(int rank,char *hostname,uint16_t port);

/*__________________________________________________________________________
*/
/**
* Init of the export -> export gateway configuration Module
  param none
  
  @retval always RUC_OK
*/
int export_expgw_conf_moduleInit();

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
void export_expgw_check_config( export_expgw_conf_ctx_t *p);

#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif

