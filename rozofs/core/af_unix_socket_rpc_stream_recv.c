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
/**
*  callback associated with the socket controller for receiving a
   message on a AF_UNIX socket operating of datagram mode

  @param socket_pointer: pointer to the socket context
  @param socketId: reference of the socket--> not used

*/
uint32_t af_unix_recv_rpc_stream_generic_cbk(void * socket_pointer,int socketId)
{

  af_unix_ctx_generic_t  *sock_p = (af_unix_ctx_generic_t*)socket_pointer;
  com_recv_template_t    *recv_p;
  uint16_t               recv_credit;
  void                  *buf_recv_p = NULL;
  int                    full_msg_len;
  uint32_t               payloadLen;
  uint8_t               *payload_p;
  uint32_t               status;
  uint32_t              *record_len_p;
  int                    len_read;
  void *bufref = NULL;
  /*
  ** set the credit, notice that the credit is decremented upon reception
  ** of a full message
  */
  recv_p = &sock_p->recv;
  recv_credit = recv_p->recv_credit_conf;

  while(recv_credit != 0)
  {
    switch (recv_p->state)
    {
      /*
      ** There is no recepition in progress
      */
      case RECV_IDLE:
        rpc->last_record   = 0;
        rpc->record_len    = 0;     
        rpc->in_tot_len    = 0;
        rpc->in_wr_offset  = 0; 
     
        recv_p->nbread = 0;
        recv_p->nb2read = recv_p->headerSize;
        recv_p->bufRefCurrent = NULL;
        recv_p->state = RECV_WAIT_HDR;
        break;
      /*
      **_________________________________________________________________
      ** Waiting for the header before allocating the receive buffer
      **_________________________________________________________________
      */
      case RECV_WAIT_HDR:
        /*
        ** attempt to receive the full header to figure out what kind of receive buffer
        ** Must be allocated
        */
        status = af_unix_recv_stream_sock_recv(sock_p,recv_p->buffer_header+recv_p->nbread,
                                               recv_p->nb2read- recv_p->nbread ,0,&len_read);
        switch(status)
        {
          case RUC_OK:
           /*
           ** that's fine : go to the next step to figure out what kind of buffer can be allocated
           */
           sock_p->stats.totalRecvBytes += len_read;
           if (recv_p->bufRefCurrent == NULL)
           {
             /*
             ** buffer has not be yet allocated
             */
             recv_p->state = RECV_ALLOC_BUF;
           }
           else
           {
             /*
             ** Buffer has already been alllocated, so it is not the fisrt received record
             ** get the header of the RPC record in order to extract the length and the type
             ** of the rpc message.
             ** The current length of the rpc record is added with the total length.
             ** Since it is not the first record, the system does not store in the receive 
             ** buffer the first 4 bytes of the rpc record. This is done for the first
             ** record only.
             */
             record_len_p = (uint32_t *)recv_p->buffer_header;    
             rpc->record_len = ntohl(*record_len_p);
             if (rpc->record_len & (~0x7fffffff)) rpc->last_record = 1;
             rpc->record_len  &= 0x7fffffff;
             rpc->in_tot_len  += rpc->record_len;           
             if ((rpc->in_tot_len+recv_p->headerSize) > rpc->max_receive_sz)
             {
                /*
                ** release the buffer
                */
                bufref = recv_p->bufRefCurrent;
                recv_p->bufRefCurrent = NULL;
                ruc_buf_freeBuffer(bufref);
                /*
                ** the length is wrong, we have no choice we need to close the connection
                */
                (sock_p->userDiscCallBack)(sock_p->userRef,sock_p->index,NULL,errno);
                recv_p->state = RECV_DEAD;
                sock_p->stats.totalRecvBadHeader++;
                /*
                ** general disconnection
                */
                af_unix_sock_stream_disconnect_internal(sock_p);
                return TRUE;
             }
             /*
             ** set the number of bytes to read and already read
             */
             recv_p->nbread = 0;
             recv_p->nb2read = rpc->record_len;    
             /*
             ** now read the payload of the record
             */
             recv_p->state = RECV_PAYLOAD;
       
           }
           break;

          case RUC_WOULDBLOCK:
           /*
           ** we don't get the full header so no change, wait for the next receiver event
           */
           return TRUE;

          case RUC_PARTIAL:
          /*
          ** update the count and re-attempt until getting a EAGAIN or a full header
          */
          sock_p->stats.totalRecvBytes += len_read;
          recv_p->nbread += len_read;
          break;

          default:
          case RUC_DISC:
          /*
          ** socket is dead call the user callback
          */
          recv_p->state = RECV_DEAD;
#warning perror print
          perror("af_unix_recv_stream_generic_cbk:");
          (sock_p->userDiscCallBack)(sock_p->userRef,sock_p->index,NULL,errno);
          /*
          ** general disconnection
          */
          af_unix_sock_stream_disconnect_internal(sock_p);
          return TRUE;
        }
      break;

      /*
      **_________________________________________________________________
      ** allocate a receive buffer according to the length of the message
      **_________________________________________________________________
      */
      case RECV_ALLOC_BUF:      
        /*
        ** store the total length of the message
        */
        record_len_p = (uint32_t *)recv_p->buffer_header;    
        rpc->record_len = ntohl(*record_len_p);
        if (rpc->record_len & (~0x7fffffff)) rpc->last_record = 1;
        rpc->record_len  &= 0x7fffffff;
        rpc->in_tot_len  += rpc->record_len;
        payloadLen = rpc->record_len;       
        /*
        ** check if the message does not exceed the max buffer size
        */
        if (rpc->last_record == 0)
        {
          /*
          ** assume max length
          */
          full_msg_len = rpc->max_receive_sz;        
        }
        else
        {
           full_msg_len = rpc->record_len + recv_p->headerSize;
        }
        if (full_msg_len > rpc->max_receive_sz)
        {
           /*
           ** the length is wrong, we have no choice we need to close the connection
           */
           (sock_p->userDiscCallBack)(sock_p->userRef,sock_p->index,NULL,errno);
           recv_p->state = RECV_DEAD;
           sock_p->stats.totalRecvBadHeader++;
           /*
           ** general disconnection
           */
           af_unix_sock_stream_disconnect_internal(sock_p);
           return TRUE;
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
           fatal("Alloc Buffer callback is mandatory for rpc service");
        }
        if (buf_recv_p == NULL)
        {
          /*
          ** the receiver is out of buffer-> leave the message in the receiver queue and exit
          */
          sock_p->stats.totalRecvOutoFBuf++;
          recv_p->state = RECV_ALLOC_BUF;
          return TRUE;
        }
        /*
        ** set the payload length in the buffer
        */
        
        ruc_buf_setPayloadLen(buf_recv_p,(uint32_t)(payloadLen + recv_p->headerSize));
        /*
        ** Ok now start receiving the payload
        */
        recv_p->nbread = recv_p->headerSize;
        recv_p->nb2read = recv_p->headerSize+payloadLen;
        recv_p->bufRefCurrent = buf_recv_p;
        payload_p = (uint8_t*)ruc_buf_getPayload(recv_p->bufRefCurrent);
        /*
        ** copy the already received bytes in the allocated received buffer
        */
        memcpy(payload_p,recv_p->buffer_header,recv_p->headerSize);
        recv_p->state = RECV_PAYLOAD;
        break;


      /*
      **_________________________________________________________________
      ** reception of the payload of the message
      **_________________________________________________________________
      */
      case RECV_PAYLOAD:
        /*
        ** attempt to receive the full header to figure out what kind of receive buffer
        ** Must be allocated
        */
        payload_p = (uint8_t*)ruc_buf_getPayload(recv_p->bufRefCurrent);
        status = af_unix_recv_stream_sock_recv(sock_p,payload_p+rpc->in_wr_offset+recv_p->nbread,
                                               recv_p->nb2read- recv_p->nbread ,0,&len_read);
        switch(status)
        {
          case RUC_OK:
           /*
           ** that fine's call the application with that received message
           */
           sock_p->stats.totalRecvBytes += len_read;
           sock_p->stats.totalRecvSuccess++;
           /*
           ** Check if it is the last record: in such a case we deliver the rpc message 
           ** to the application
           */
           if (rpc->last_record)
           {
             /*
             ** clear the reference of the buffer to avoid a double release that may occur
             ** if the application delete the context
             ** Update the first length header of the rpc message with the total length of the
             ** RPC message
             */
             bufref = recv_p->bufRefCurrent;
             recv_p->bufRefCurrent = NULL;
             ruc_buf_setPayloadLen(bufref,(uint32_t)(rpc->in_tot_len + recv_p->headerSize));
             record_len_p = (uint32_t *)payload_p;  
             *record_len_p = htonl((rpc->in_tot_len) | 0x80000000));                          
             (sock_p->userRcvCallBack)(sock_p->userRef,sock_p->index,bufref);
             recv_p->state = RECV_IDLE;
             recv_credit--;
             break;
           }
           /*
           ** not the last record:
           ** udpate the in_wr_offset with the current record length and prepare to receive
           ** the next record
           */
           rpc->in_wr_offset+= rpc->record_len;           
           recv_p->state = RECV_WAIT_HDR;
           break;

          case RUC_WOULDBLOCK:
           /*
           ** we don't get the full message so no change, wait for the next receiver event
           */
           return TRUE;

          case RUC_PARTIAL:
          /*
          ** update the count and re-attempt until getting a EAGAIN or a full header
          */
          sock_p->stats.totalRecvBytes += len_read;
          recv_p->nbread += len_read;
          break;

          case RUC_DISC:
          default:
          /*
          ** socket is dead call the user callback
          */
          bufref = recv_p->bufRefCurrent;
          recv_p->bufRefCurrent = NULL;
          ruc_buf_freeBuffer(bufref);

          /*
          ** it is up to the application to release the buffer if the error is fatal
          ** but for the case of the receiver, no buffer reference is provided
          */
          recv_p->state = RECV_DEAD;
          (sock_p->userDiscCallBack)(sock_p->userRef,sock_p->index,NULL,errno);
           /*
          ** general disconnection
          */
          af_unix_sock_stream_disconnect_internal(sock_p);
          return TRUE;
        }
        break;
      /*
      **_________________________________________________________________
      ** Dead state of the receiver
      **_________________________________________________________________
      */
      case RECV_DEAD:
        return TRUE;
    }
  }
  return TRUE;
}
