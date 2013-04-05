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
#ifndef RUC_TCP_CLIENT_API_H
#define RUC_TCP_CLIENT_API_H

#include <stdio.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_list.h"
#include "ruc_buffer_api.h"
#include "uma_tcp_main_api.h"




/*
**     P U B L I C
*/

/*
**-------------------------------------------------------
  uint32_t ruc_tcp_clientinit(uint32 nbElements)
**-------------------------------------------------------
**  #SYNOPSIS
**   That function allocates all the necessary
**   client TCP connections management
**
**   IN:
**       nbElements : number ofclient TCP connections
**                    supported
**
**
**   OUT :
**      RUC_NOK : error during the initialization
**      RUC_OK : module up and ready.
**
**-------------------------------------------------------
*/

uint32_t ruc_tcp_client_init(uint32_t nbElements);


/*-----------------------------------------------------------------
**
**
** uint32_t ruc_tcp_client_bindClient(ruc_tcp_client_create_t *pconf)
**
**------------------------------------------------------------------
**  #SYNOPSIS
**    That service is intended to be used by application that
**    open a TCP connection in client mode
**    The input parameters are:
**       IPaddr and TCP port of the destination
**       CallBack on Connect : (user reference, TCP connection ref, status)
**            if the status is RUC_NOK: the connect has failed
**            if status is RUC_OK the connect is OK and the application
**            can start sending and receiving data on TCP connection ref
**
**      CallBack for Recv : (user reference, TCP connection ref, bufref)
**      CallBack for Disc (user ref, TCP connection ref)
**      CallBack for receiver Ready (user Ref,socketId)
**        return TRUE for ready and FALSE otherwise
**
**
**   OUT :
**      != -1 index of the connection
**      ==-1 error
**
**------------------------------------------------------------------
*/
typedef struct _ruc_tcp_client_create_t
{
  uint32_t        headerSize;       /* size of the header to read                 */
  uint32_t        msgLenOffset;     /* offset where the message length fits       */
  uint32_t        msgLenSize;       /* size of the message length field in bytes  */
  uint32_t        bufSize;         /* length of buffer (xmit and received)        */
  uma_tcp_recvCBK_t   userConnectCallBack; /* callBack when TCP connect is done         */
  uma_tcp_recvCBK_t   userRcvCallBack;   /* callback provided by the connection owner block */
  uma_tcp_discCBK_t   userDiscCallBack; /* callBack for TCP disconnection detection         */
  ruc_pf_sock_t       userRcvReadyCallBack; /* callBack for TCP receiver ready         */
  uint32_t        userRef;           /* user reference that must be recalled in the callbacks */
  uint32_t        IpAddr;           /* host format */
  uint16_t	tcpDestPort;      /* host format */
  uint32_t        srcIP;            /* host format */
  uint16_t	srcTcp;           /* host format */
} ruc_tcp_client_create_t;

uint32_t ruc_tcp_client_bindClient(ruc_tcp_client_create_t *pconf);


/*-----------------------------------------------------------------
**
**
** uint32_t ruc_tcp_client_bindClientWithSrcIp(ruc_tcp_client_create_t *pconf)
**
**------------------------------------------------------------------
**  #SYNOPSIS
**    That service is intended to be used by application that
**    open a TCP connection in client mode. With that API
**    the caller is intended to provide a SRC IP and a src TCP
**    port in the connection interface
**    The input parameters are:
**       IPaddr and TCP port of the destination (host format)
**       srcIP : source IP address, Not significant if ANY_ADDR
**       srcTcp : source TCP port
**
**  note: IP addresses and TCP ports MUST be provided in HOST FORMAT !!!!!
**
**       CallBack on Connect : (user reference, TCP connection ref, status)
**            if the status is RUC_OK: the connect has failed
**            if status is RUC_OK the connect is OK and the application
**            can start sending and receiving data on TCP connection ref
**
**      CallBack for Recv : (user reference, TCP connection ref, bufref)
**      CallBack for Disc (user ref, TCP connection ref)
**      CallBack for receiver Ready (user Ref,socketId)
**        return TRUE for ready and FALSE otherwise
**
**
**   OUT :
**      != -1 index of the connection
**      ==-1 error
**
**------------------------------------------------------------------
*/

uint32_t ruc_tcp_client_bindClientWithSrcIp(ruc_tcp_client_create_t *pconf);

/*
**-------------------------------------------------------
  uint32_t ruc_tcp_client_delete_connection(uint32 clientIdx)
**-------------------------------------------------------
**  #SYNOPSIS
**  that function is called to delete all the information
** related to the TCP connection that is reference by
** ClientIdx (reference has been returned by
**  the ruc_tcp_client_bindClient() service)
**
**   IN:
**       clientIdx : reference of the connection context
**
**
**   OUT :
**        none
**
**-------------------------------------------------------
*/
void ruc_tcp_client_delete_connection(uint32_t clientIdx);


/*
**-------------------------------------------------------
  uint32_t ruc_tcp_client_restart_connect(uint32 clientIdx)
**-------------------------------------------------------
**  #SYNOPSIS
**  Restart of a TCP connection. That service must be
**  called when there was an error either on the
** connect() or when a disconnect event has been
** received.
**
**   IN:
**       clientIdx : reference of the connection context
**
**
**   OUT :
**        none
**
**-------------------------------------------------------
*/
uint32_t ruc_tcp_client_restart_connect(uint32_t clientIdx);
#endif
