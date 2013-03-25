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
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ppu_trace.h"
#include "af_unix_socket_generic_api.h"


/**
*  callback associated with the socket controller for receiving a
   message on a AF_UNIX socket operating of datagram mode

  @param socket_pointer: pointer to the socket context
  @param socketId: reference of the socket--> not used

*/
uint32_t af_unix_recv_generic_cbk(void * socket_pointer,int socketId)
{

  uint8_t                buffer_header[ROZOFS_MAX_HEADER_SIZE];
  af_unix_ctx_generic_t  *sock_p = (af_unix_ctx_generic_t*)socket_pointer;
  int                    bytesRcvd;
  struct sockaddr_un     sockAddr;
  int                    sockAddrLen;
  com_recv_template_t    *recv_p;
  uint16_t               recv_credit;
  int                    eintr_count;
  void                  *buf_recv_p;
  int                    full_msg_len;
  uint32_t               payloadLen;
  char                  *sockname_p;
  uint8_t               *payload_p;

  recv_p = &sock_p->recv;
  recv_credit = recv_p->recv_credit_conf;


  sockAddrLen = sizeof(sockAddr);
  /*
  ** Get the header of the message without removing the message from the
  ** socket queue
  */
  eintr_count = 0;
  while(recv_credit != 0)
  {
    bytesRcvd = recvfrom(sock_p->socketRef,
		        buffer_header,
		        recv_p->headerSize,
		        MSG_PEEK,
		        (struct sockaddr *)&sockAddr,
		        ( socklen_t *)&sockAddrLen);
   if (bytesRcvd == -1)
   {
     switch (errno)
     {
       case EAGAIN:
        /*
        ** the socket is empty
        */
        return TRUE;

       case EINTR:
         /*
         ** re-attempt to read the socket
         */
         eintr_count++;
         if (eintr_count < 3) continue;
         /*
         ** here we consider it as a error
         */
         RUC_WARNING(eintr_count);
         sock_p->stats.totalRecvError++;
         return TRUE;

       case EBADF:
       case EFAULT:
       case EINVAL:
       default:
         /*
         ** We might need to double checl if the socket must be killed
         */
         RUC_WARNING(errno);
         sock_p->stats.totalRecvError++;
         return TRUE;
     }
   }
   /*
   ** check we have the right length
   */
   if (bytesRcvd != recv_p->headerSize)
   {
     /*
     ** unable to read the full header, so drop the current message. Notice that for
     ** the AF_UNIX case, the message will be truncated to the max size of buffer_header
     ** Here we will indicate an error an attempt to read the next inflight message if any
     */
     sock_p->stats.totalRecvBadHeader++;
     RUC_WARNING(bytesRcvd);

     recvfrom(sock_p->socketRef,
		      buffer_header,
		      recv_p->headerSize,
		      0,
		      (struct sockaddr *)&sockAddr,
		      ( socklen_t *)&sockAddrLen);
      sock_p->stats.totalRecvBadHeader++;
      recv_credit--;
      continue;
   }
   /*
   ** get the length of the payload of the message
   */
   payloadLen = com_sock_extract_length_from_header_host_format((char*)buffer_header,recv_p->msgLenOffset,recv_p->msgLenSize);
   if (payloadLen == 0)
   {
     /*
     ** the length information is wrong, skip that message and attempt to read the next one
     ** in sequence
     */
     RUC_WARNING(payloadLen);

     recvfrom(sock_p->socketRef,
		      buffer_header,
		      recv_p->headerSize,
		      0,
		      (struct sockaddr *)&sockAddr,
		      ( socklen_t *)&sockAddrLen);
      sock_p->stats.totalRecvBadHeader++;
      recv_credit--;
      continue;
   }
   /*
   ** check if the message does not exceed the max buffer size
   */
   full_msg_len = payloadLen + recv_p->headerSize;
   if (full_msg_len > recv_p->bufSize)
   {
     /*
     ** message exceeds the capacity of the receiver-> drop it
     */
     RUC_WARNING(payloadLen);

     recvfrom(sock_p->socketRef,
		      buffer_header,
		      recv_p->headerSize,
		      0,
		      (struct sockaddr *)&sockAddr,
		      ( socklen_t *)&sockAddrLen);
      sock_p->stats.totalRecvBadLength++;
      recv_credit--;
      continue;
   }
   /*
   ** Ok now call the application for receive buffer allocation
   */
   if (sock_p->userRcvAllocBufCallBack != NULL)
   {
      buf_recv_p = (sock_p->userRcvAllocBufCallBack)(sock_p->userRef,sock_p->index,full_msg_len);
   }
   else
   {
     buf_recv_p = af_unix_alloc_recv_buf();
   }
   if (buf_recv_p == NULL)
   {
     /*
     ** the receiver is out of buffer-> leave the message in the receiver queue and exit
     */
     sock_p->stats.totalRecvOutoFBuf++;
     return TRUE;
   }
   payload_p = (uint8_t*)ruc_buf_getPayload(buf_recv_p);
   /*
   ** OK, we have a receive buffer, so receive the full message and give to the application
   ** but before read it from the socket
   */
  eintr_count = 0;
retry:
   bytesRcvd = recvfrom(sock_p->socketRef,
		       payload_p,
		       full_msg_len,
		       0,
		       (struct sockaddr *)&sockAddr,
		       ( socklen_t *)&sockAddrLen);
    if (bytesRcvd == -1)
    {
      switch (errno)
      {
        case EAGAIN:
         /*
         ** the socket is empty--> that situation MUST not occur:
         */
         ruc_buf_freeBuffer(buf_recv_p);
         return TRUE;
        case EINTR:
          /*
          ** re-attempt to read the socket
          */
          eintr_count++;
          if (eintr_count < 3) goto retry;
          /*
          ** here we consider it as a error
          */
          RUC_WARNING(eintr_count);
          /*
          ** release the receive buffer
          */
          ruc_buf_freeBuffer(buf_recv_p);
          sock_p->stats.totalRecvError++;
          return TRUE;

        case EBADF:
        case EFAULT:
        case EINVAL:
        default:
          /*
          ** We might need to double checl if the socket must be killed
          */
          ruc_buf_freeBuffer(buf_recv_p);
          RUC_WARNING(errno);
          sock_p->stats.totalRecvError++;
          return TRUE;
      }
    }
    /*
    ** check we have the right length
    */
    if (bytesRcvd != full_msg_len)
    {
      /*
      ** that situation must not occur with datagram socket
      */
      RUC_WARNING(errno);
      ruc_buf_freeBuffer(buf_recv_p);
      sock_p->stats.totalRecvError++;
      recv_credit--;
      continue;
    }
    /*
    ** OK, copy the reference of the source in the packet buffer
    */
    sockname_p = ruc_buf_get_usrSrcInfo(buf_recv_p);
    memcpy(sockname_p,&sockAddr.sun_path,sockAddrLen);
    /*
    ** OK, call the receive process of the application
    */
    sock_p->stats.totalRecvBytes += full_msg_len;
    sock_p->stats.totalRecvSuccess++;
    (sock_p->userRcvCallBack)(sock_p->userRef,sock_p->index,buf_recv_p);
    recv_credit--;
  }
  return TRUE;
}
