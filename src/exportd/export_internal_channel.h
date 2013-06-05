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

#ifndef EXPORT_INTERNAL_CHANNEL_H
#define EXPORT_INTERNAL_CHANNEL_H
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
#include <rozofs/rpc/gwproto.h>


typedef struct expgwc_internal_channel_conf_t
{
  int       internalSocket[2];  /* -1 if N.S */
  void *    intSockconnectionId[2];  /* -1: connection id returned by the sock Ctrl */
} expgwc_internal_channel_conf_t;


#define EXPORTDNB_INTERNAL_CHAN_SEND_SZ_MAX  (1024*32)  /**<  xmit buffer size  for the internal channel*/
#define EXPORTDNB_INTERNAL_CHAN_RECV_SZ_MAX  (1024*32)  /**<  receive buffer size  for the internal channel*/

typedef enum
{
  EXPGWC_NULL = 0,
  EXPGWC_LOAD_CONF = 1 /**< opcode for adding the export gateway configuration  */

} expgwc_internal_channel_opcode_e;

/**
* message structue echanged between the thread that monitor the port configuration of
* a load balancing group and the non blocking entity
* The goal of the exchange is to trigger the configuration of the ports once the 
* the storage provides its listening ports
*/
typedef struct _expgwc_int_chan_msg_t
{
    uint32_t  opcode;
    uint32_t  length; /**< length of the payload */
} expgwc_int_chan_msg_t;


extern gw_configuration_t  expgw_conf_local;
extern int expgw_configuration_available;
/*
 *_______________________________________________________________________
 */
/**
* API to return the max payload length for internal channel xmit

  @param none
  
  @retval effective payload length of the buffer
*/
static inline uint32_t expgwc_get_internal_channel_buf_send_size()
{
  return (EXPORTDNB_INTERNAL_CHAN_SEND_SZ_MAX - sizeof(expgwc_int_chan_msg_t));
}



/*
 *_______________________________________________________________________
 */
/**
*  send a message on the internal channel

  The message to send is copied in a pre-allocated buffer 
  The message must not exceed the size of the pre-allocated buffer
  otherwise the message is rejected
  
  If the message has been allocated by a malloc() it is up to the calling
  function to release it.

 @param opcode : opcode of the message to send
 @param length : length of the message
 @param message : pointer to the message to send
 
 @retval 0 : success
 @retval <0 : error (see errno)
*/
int expgwc_internal_channel_send(uint32_t opcode,uint32_t length, void *message );


/*
 *_______________________________________________________________________
 */
 
expgwc_internal_channel_conf_t *expgwc_sup_getObjRef();

uint32_t  expgwc_sup_getIntSockIdxFromSocketId(expgwc_internal_channel_conf_t *p,int socketId);
uint32_t expgwc_sup_rcvReadyInternalSock(void * not_significant,int socketId);
uint32_t expgwc_internal_channel_recv_cbk(void * not_significant,int socketId);
uint32_t expgwc_sup_xmitReadyInternalSock(void * not_significant,int socketId);
uint32_t expgwc_sup_xmitEvtInternalSock(void * not_significant,int socketId);
uint32_t expgwc_sup_createInternalSocket(expgwc_internal_channel_conf_t *p);
uint32_t expgwc_sup_deleteInternalSocket(expgwc_internal_channel_conf_t *p);
uint32_t expgwc_int_chan_moduleInit();

#endif
