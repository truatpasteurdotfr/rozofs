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

#ifndef ROZOFS_EXPORT_GATEWAY_CONF_NON_BLOCKING_H
#define ROZOFS_EXPORT_GATEWAY_CONF_NON_BLOCKING_H
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>

#include "config.h"
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>

#include <rozofs/common/types.h>
#include <rozofs/core/ruc_common.h>
#include <rozofs/core/ruc_sockCtl_api.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/eclient.h>

typedef struct rozofs_exp_sup_conf_t
{
  int       internalSocket[2];  /* -1 if N.S */
  void *    intSockconnectionId[2];  /* -1: connection id returned by the sock Ctrl */
} rozofs_exp_sup_conf_t;


#define ROZOFS_EXPORT_GW_CONF     1  /**< opcode to indicate that the message contains the export gateways conf.  */
/**
* message structue echanged between the thread that monitor the port configuration of
* a load balancing group and the non blocking entity
* The goal of the exchange is to trigger the configuration of the ports once the 
* the storage provides its listening ports
*/
typedef struct _rozofs_exp_msg_t
{
    uint32_t  opcode;
    uint32_t  filler;
    void      *param;
} rozofs_exp_msg_t;


rozofs_exp_sup_conf_t *rozofs_exp_getObjRef();
uint32_t  rozofs_exp_getIntSockIdxFromSocketId(rozofs_exp_sup_conf_t *p,int socketId);
uint32_t rozofs_exp_rcvReadyInternalSock(void * not_significant,int socketId);
uint32_t rozofs_exp_rcvMsgInternalSock(void * not_significant,int socketId);
uint32_t rozofs_exp_xmitReadyInternalSock(void * not_significant,int socketId);
uint32_t rozofs_exp_xmitEvtInternalSock(void * not_significant,int socketId);
uint32_t rozofs_exp_createInternalSocket(rozofs_exp_sup_conf_t *p);
uint32_t rozofs_exp_deleteInternalSocket(rozofs_exp_sup_conf_t *p);
int rozofs_exp_send_lbg_create(uint32_t opcode, void *mstorage );


/*
**_________________________________________________________________________
*/
/**
*  Process the configuration of the export gateway received from the 
   main process:
   Upon the reception of the export gateway from the exportd , the configuration
   supervision thread sends the decoded received configuration to the non-blocking
   part of the rozofsmount.
   That message is received on an AF_UNIX socket (socket pair).
   Upon processing the configuartion, the non blocking side sends back a status to
   the supervision thread.
   
   
  @param p: pointer to the socket pair context
  @param msg: pointer to the received message
  
  @retval 0 on success
  @retval -1 on error
*/
int rozofs_exp_process_export_gateway_conf_nb(rozofs_exp_sup_conf_t *p,rozofs_exp_msg_t *msg);
/*
**_________________________________________________________________________
*/
/**
*  send an export gateway configuration message udpate/creation
   for the main thread

  @param expgw_conf_p: pointer to the export gateway configuration (decoded)
  
  @retval 0 on success
  @retval -1 on error
*/
int rozofs_exp_reload_export_gateway_conf(ep_gateway_configuration_t *expgw_conf_p );


uint32_t rozofs_exp_moduleInit();

#endif
