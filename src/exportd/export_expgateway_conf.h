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
  EXPGW_NOT_CONFIGURED = 0,
  EXPGW_CONFIGURED,
} export_expgwc_conf_state_e;

typedef struct export_expgw_conf_ctx_t
{
  ruc_obj_desc_t    link;          /**< To be able to chain the  context on any list */
  uint32_t            index;         /**< rank of the gateway in the configuration (must be the index of the context     */
  uint32_t            free;          /**< Is the context free or allocated TRUE/FALSE    */
  /*
  ** specific part
  */
   uint16_t  port;     /**< tcp port in host format   */
   uint32_t  ipaddr;   /**< IP address in host format */
   int       current_conf_idx;
   char      hostname[ROZOFS_HOSTNAME_MAX];
   int       gateway_lbg_id;        /**< reference of the load balancing group for reaching the configuration port of the gateway */
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


#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif

