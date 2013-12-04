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
#ifndef RUC_TCP_CLIENT_H
#define RUC_TCP_CLIENT_H

#include <stdio.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_list.h"
#include "ruc_buffer_api.h"
#include "uma_tcp_main_api.h"
#include "ruc_tcp_client_api.h"


#define UMA_TCP_TIMEOUT_VAL 10   /* 10 secs */

#define UMA_TCP_IDLE  0
#define UMA_TCP_ACTIVE 1
#define UMA_TCP_DISCONNECTING 2



/*
**  data structure
*/

typedef struct _ruc_tcp_client_t
{
  ruc_obj_desc_t            link;
  uint32_t		    ref;
  /*
  **  configuration block for TCP connection opening
  */
  uma_tcp_create_t          conf;
  /*
  ** internal variables
  */
  uint32_t                    tcpSocketClient;
//64BITS   uint32_t                    tcpCnxClient;
  void                     *tcpCnxClient;
  uint32_t                    tcpCnxIdx;
  uint32_t                    tcpTimeOut;
  uint32_t                    tcpstate;
  /*
  ** user configuration parameters
  */
  uint32_t        headerSize;       /* size of the header to read                 */
  uint32_t        msgLenOffset;     /* offset where the message length fits       */
  uint32_t        msgLenSize;       /* size of the message length field in bytes  */
  uint32_t        bufSize;         /* length of buffer (xmit and received)        */
  uma_tcp_recvCBK_t         userConnectCallBack;   /* callBack when TCP connect is done         */
  uma_tcp_recvCBK_t         userRcvCallBack;      /* callback provided by the connection owner block */
  uma_tcp_discCBK_t         userDiscCallBack;     /* callBack for TCP disconnection detection         */
  ruc_pf_sock_t             userRcvReadyCallBack; /* callback for receiver ready */
  uint32_t        userRef;           /* user reference that must be recalled in the callbacks */
  uint32_t        IpAddr;
  uint16_t	tcpDestPort;
  uint16_t	srcTcp;   /* in host format */
  uint32_t        srcIP;    /* in host format */
  uint16_t        filler;

} ruc_tcp_client_t;


/*
**   P R I V A T E
*/


/*----------------------------------------------
   ruc_tcp_clientgetObjRef
**---------------------------------------------
** that function returns the pointer to the
** TCP CNX context
**
**  IN : tcpRef : TCP CNX object reference
**
**  OUT: NULL : out of range index
**       !=NULL : pointer to the object
**----------------------------------------------
*/
ruc_tcp_client_t *ruc_tcp_clientgetObjRef(uint32_t ObjRef);

/*
** call backs
*/
uint32_t ruc_tcp_client_connectReply_CBK(void * tcpClientCnxRef,int socketId);
uint32_t ruc_tcp_clientisRcvReady(void *ref,int socketId);
uint32_t ruc_tcp_clientisXmitReady(void * ref,int socketId);
uint32_t ruc_tcp_clientfake(void * ref,int socketId);
/*
**--------------------------------------------------------------------------
  uint32_t uc_tcp_client_receiveCBK(uint32 userRef,uint32 tcpCnxRef,uint32 bufRef)
**--------------------------------------------------------------------------
**  #SYNOPSIS
**   callback used by the TCP connection receiver FSM
**   when a message has been fully received on the
**   TCP connection.
**
**   When the application has finsihed to process the message, it must
**   release it
**
**   IN:
**       user reference provide at TCP connection creation time
**       reference of the TCP objet on which the buffer has been allocated
**       reference of the buffer that contains the message
**
**   OUT : none
**
**
**--------------------------------------------------------------------------
*/
//64BITS void ruc_tcp_client_receiveCBK(void *opaque,uint32_t tcpCnxRef,uint32 bufRef);
void ruc_tcp_client_receiveCBK(void *opaque,uint32_t tcpCnxRef,void *bufRef);


uint32_t ruc_tcp_client_socketCreate(ruc_tcp_client_t *pObj);
//64BITS void ruc_tcp_client_disc_CBK(uint32_t refObj,uint32 tcpCnxRef);
void ruc_tcp_client_disc_CBK(void *refObj,uint32_t tcpCnxRef);


/*
**
*/


#endif
