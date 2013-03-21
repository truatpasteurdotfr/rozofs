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
** I N C L U D E S
*/
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_list.h"
#include "ruc_buffer_api.h"
#include "ruc_trace_api.h"

#include "uma_fsm_framework.h"
#include "uma_tcp.h"
#include "ppu_trace.h"
//#include "uma_mon_api.h"


/*
**   D E F I N I T I O N S
*/

/*
** FSM States
*/
#define STATE_BEGIN  1
#define STATE_WAIT_HEADER 2
#define STATE_HEADER_READING 3
#define STATE_PAYLOAD_READING 4
#define STATE_WAITPAYLOAD 5
#define STATE_MESSAGE_RECEIVED 6
#define STATE_DISCONNECTING 7
#define STATE_DISCONNECTED 8
#define STATE_BUFFER_DEPLETION 9
#define STATE_DELETING 10
#define STATE_DELETED 11

/*__________________________________________________________________________
  FUNCTION: tcp_rcvFsm_state2String
  Returns a string from a value within a list of constants
  Generated on 2005/03/25
  ==========================================================================
  PARAMETERS:
  - x: value to translate into a string
  RETURN: a string STATE_DELETING
  ==========================================================================*/
char * tcp_rcvFsm_state2String (int x) {

  switch(x) {
  case STATE_BEGIN: return "STATE_BEGIN";
  case STATE_WAIT_HEADER: return "STATE_WAIT_HEADER";
  case STATE_HEADER_READING: return "STATE_HEADER_READING";
  case STATE_PAYLOAD_READING: return "STATE_PAYLOAD_READING";
  case STATE_WAITPAYLOAD: return "STATE_WAITPAYLOAD";
  case STATE_MESSAGE_RECEIVED: return "STATE_MESSAGE_RECEIVED";
  case STATE_DISCONNECTING: return "STATE_DISCONNECTING";
  case STATE_DISCONNECTED: return "STATE_DISCONNECTED";
  case STATE_BUFFER_DEPLETION: return "STATE_BUFFER_DEPLETION";
  case STATE_DELETING: return "STATE_DELETING";
  case STATE_DELETED: return "STATE_DELETED";

    /* Value out of range */
  default: return "?unknown?";
  }
}

/*__________________________________________________________________________
  Display an history record
  ==========================================================================
  PARAMETERS:
  . printState : whether the state is to be displayed
  RETURN: none
  ==========================================================================*/
void uma_tcp_rcvFsm_printHistRec( uint8_t printState, uint32_t flag0, uint32_t flag1) {
  int32_t  idx = 0;
  uint32_t mask = 0x80000000;

  uint8_t state = flag1 &0xFF;

  if ((flag0 == 0) && (flag1 == 0)) return;

  flag1 = flag1 & 0xFFFFFF00;

  if (printState) {
    printf ("TCP RCV         V\n");
    printf ("TCP RCV       %s ", tcp_rcvFsm_state2String(state));
  }
  else {
    printf ("TCP RCV         | ");
  }

  /* Display the 32 flags of 1rst word */
  if (flag0 != 0) {
    mask = 0x80000000;
    for (idx=31; idx>=0; idx--) {
      if (flag0 & mask) {
	if (tcpFlagBits0[idx] != NULL) printf("%s ", tcpFlagBits0[idx]);
      }
      mask = mask>>1;
    }
  }
  /* Display the 24 flags of 2nd word
  if (flag1 != 0) {
    mask = 0x80000000;
    for (idx=31; idx>=0; idx--) {
      if (flag1 & mask) {
	if (tcpFlagBits1[idx] != NULL) printf("%s ", tcpFlagBits1[idx]);
      }
      mask = mask>>1;
    }
  }  */
  printf ("\n");
}

/*
**  F U N C T I O N
*/


void uma_tcp_eval_rcv_otherFlags(uma_tcp_t *pObj)
{

   pObj->rcvBufPoolEmpty = ruc_buf_isPoolEmpty(pObj->rcvBufHead);
}


/*
**  That procedure returns the current length of message received. This
**  correspond to the remaining length without the header
*/

uint32_t uma_tcp_extract_rcvMsgLen(char *pbuf,uint32_t offset,uint32_t fieldLen)
{
   uint16_t word16;
   uint32_t word32;
   uint8_t * p;

   p = (uint8_t*)(pbuf+offset);
   switch (fieldLen)
   {
     case 1:
	return (uint32_t)*p;
     case 2:
	 word16 = (p[0]<<8)+ p[1];
	 return (uint32_t) word16 /*(ntohs(word16))*/;
     case 4:

         word32 = (uint32_t)p[0];
         word32 = (word32 << 8)| (p[1]);
     	 word32 = (word32 << 8)| (p[2]);
     	 word32 = (word32 << 8)| (p[3]);

	 return (uint32_t)word32; /*(uint32)(ntohl(word32));*/
     default:
	ERRLOG "Bad size of header field length %d\n", fieldLen ENDERRLOG
	return 0;
   }
}
/*
** Signals assertion
*/


uint32_t uma_tcp_rcvFsm_begin(uma_tcp_t *pObj)
{
   pObj->rcvFsm.fsm_state = STATE_BEGIN;
   uma_fsm_engine(pObj,&pObj->rcvFsm);
   return (RUC_OK);
}





void uma_tcp_rcvFsm_msgInAssert(uma_tcp_t *pObj)
{

   pObj->msg_in = TRUE;
   uma_fsm_engine(pObj,&pObj->rcvFsm);
}





uint32_t uma_tcp_readSocket  (uma_tcp_t *pObj,
                             uma_fsm_t  *pfsm)
{
  int ret;
  char *pbuf;
  uint32_t len;
  /*
  ** get the payload and length of the receive buffer
  ** and then adjust it according to the current number
  ** of bytes already sent
  */
  pbuf = (char*)ruc_buf_getPayload(pObj->rcvBufRef);
  if (pbuf == (char*)NULL)
  {
    /*
    ** we are dead !!
    */
    RUC_WARNING(pObj->rcvBufRef);
    uma_tcp_sockDiscAssert(pObj);
    return FALSE;
  }
  len = ruc_buf_getPayloadLen(pObj->rcvBufRef);
  pbuf +=len;
  /*
  ** read the socket
  */
  ret=recv((int)pObj->socketRef,pbuf,(int)(pObj->nbToRead-pObj->nbRead),0);
  if (ret < 0)
  {
    /*
    ** 2 cases only) WOULD BLOCK or deconnection
    */
    if (errno==EWOULDBLOCK)
    {
      /*
      ** socket is empty, nothing read
      */
      return FALSE;
    }
    else
    {
      INFO_PRINT "REMOTE DISCONNECTION %s - errno %u : %s",
	pObj->cnxName, errno, strerror(errno)
	RUC_EINFO;
      uma_tcp_monTxtCbk("TCP","REMOTE DISCONNECTION %s - errno %u : %s",
			pObj->cnxName, errno, strerror(errno));
      uma_tcp_sockDiscAssert(pObj);
      return FALSE;
    }
  }
  /*
  **  FDL) the FSM_STATE_BEGIN (of reading 0 byte is
  **  considered as a TCP deconnection
  */
  if (ret == 0)
  {
     INFO_PRINT "REMOTE DISCONNECTION %s",
       pObj->cnxName
       RUC_EINFO;
     uma_tcp_monTxtCbk("TCP","REMOTE DISCONNECTION %s",
		       pObj->cnxName);
     uma_tcp_sockDiscAssert(pObj);
      return FALSE;
  }

  /*
  ** it is OK) update the payload length
  */
  pObj->nbRead +=(uint16_t)ret;
  if (pObj->nbRead == pObj->nbToRead)
  {
    /*
    ** the full message has been read
    ** update the buffer payload
    */
    len = ruc_buf_getPayloadLen(pObj->rcvBufRef);
    len +=(uint32_t)ret;
    ruc_buf_setPayloadLen(pObj->rcvBufRef,len);
    /*
    **  update receive bytes counter
    */
    pObj->stats.nbByteRcv +=len;
    return TRUE;
  }
  /*
  ** only some bytes have been read. It is considered as
  ** a congestion case. The number of bytes that have
  ** been read is updated
  */
  len = ruc_buf_getPayloadLen(pObj->rcvBufRef);
  len +=(uint32_t)(uint32_t)ret;
  ruc_buf_setPayloadLen(pObj->rcvBufRef,len);

  return FALSE;
}

/*----------------------------------------------
   uma_tcp_rcvFsm_check_bufferDepletion
**---------------------------------------------
** that function returns the pointer to the
** TCP CNX context
**
**  IN : tcpRef : TCP CNX object reference
**
**  OUT: TRUE : if out of rcv buffer and count not zero
**       FALSE
**----------------------------------------------
*/

uint32_t uma_tcp_rcvFsm_check_bufferDepletion(uma_tcp_t *pObj)
{
  if (pObj->rcvFsm.fsm_state != STATE_BUFFER_DEPLETION)
  {
    return FALSE;
  }
  pObj->depletionCounter--;
  if (pObj->depletionCounter <= 0)
  {
    return FALSE;
  }
  return TRUE;
}

/*----------------------------------------------
uma_tcp_rcvFsm_execute
**---------------------------------------------
** TCP FSM state machine
**----------------------------------------------
*/
void uma_tcp_rcvFsm_execute(uma_tcp_t *pObj,
                    uma_fsm_t  *pfsm)
{
  char   *prcvMsg;     /* ptr to msg relu struct */

  while(1)
  {

    /*
    ** Global transition)if a disconnect is encountered
       the transmit queue is purged and if there is
       any buffer pending in transmit process or a buffer
       to be sent, it is released.
    */
    while(1)
    {
      FSM_TRANS_BEGIN ((pObj->sockDiscRcv==TRUE)&&(pObj->rcvLock==FALSE))

      pObj->sockDiscRcv = FALSE;
      /*
      ** UCT to disconnecting
      */
      FSM_SET(pfsm,STATE_DISCONNECTING);
      FSM_TRANS_END
      /*
      ** test if a reset of the FSM has been request
      */
      FSM_TRANS_BEGIN (pObj->rcvReset==TRUE)

      pObj->rcvReset = FALSE;
      /*
      ** UCT to STATE DELETING
      */
      FSM_SET(pfsm,STATE_DELETING);
      FSM_TRANS_END

      FSM_TRANS_BEGIN (pObj->rcvRestart==TRUE)
        /*
        ** return to the current state
        */
        pObj->rcvRestart = FALSE;
	pObj->rcvLock = FALSE;
	pObj->nbRead =  0;
        /*
        **  free the current receive buffer
        */
        if (pObj->rcvBufRef != NULL) {
          ruc_buf_freeBuffer(pObj->rcvBufRef);
          pObj->rcvBufRef = NULL;
	}
	FSM_SET(pfsm,STATE_WAIT_HEADER)

      FSM_TRANS_END
      break;
    }
    switch(pfsm->fsm_state)
    {
      FSM_STATE_BEGIN (STATE_BEGIN)
        FSM_ACTION_BEGIN
        /*
        ** fsm initialization
        */


        FSM_SET(pfsm,STATE_WAIT_HEADER);
        FSM_ACTION_END
	/*
        **  UCT transition
        */
      FSM_STATE_END

      /*
      ** ----------------------------------
      **   STATE_WAIT_HEADER
      **-----------------------------------
      */

      FSM_STATE_BEGIN (STATE_WAIT_HEADER)
        FSM_TRANS_BEGIN (pObj->msg_in==TRUE)
          pObj->msg_in = FALSE;
          /*
          ** something to read
          */
          if (pObj->rcvBufRef ==  NULL)
          {
            /*
            **  allocated a buffer
            */
            pObj->rcvBufRef = ruc_buf_getBuffer(pObj->rcvBufHead);
            if (pObj->rcvBufRef == NULL)
            {
              /*
              ** something is rotten, assert the
              ** socket disconnection signal for
              ** every body
              */
              FSM_SET(pfsm,STATE_BUFFER_DEPLETION);
            }
            /*
            ** indicates the number of bytes to read
            */
            pObj->nbToRead=pObj->headerSize;
            pObj->nbRead =  0;
          }
          /*
          ** call the receive function
          */
          pObj->header_rcv = uma_tcp_readSocket(pObj,pfsm);
          FSM_SET(pfsm,STATE_HEADER_READING);
        FSM_TRANS_END
      FSM_STATE_END


      /*
      ** ----------------------------------
      **   STATE_HEADER_READING
      **-----------------------------------
      */

      FSM_STATE_BEGIN (STATE_HEADER_READING)
        /*
        **
        */
	FSM_TRANS_BEGIN (pObj->header_rcv == TRUE)
        {
          /*
          ** a full header has been read. Now
          ** get the payload
          */
          prcvMsg = ruc_buf_getPayload(pObj->rcvBufRef);

          pObj->nbToRead = (uint16_t)uma_tcp_extract_rcvMsgLen(prcvMsg,
	                                                     pObj->msgLenOffset,
							     pObj->msgLenSize);

	  /*
	  ** Check that the message length do not exceed the buffer size
	  */
	  if (pObj->nbToRead > (uint16_t)(pObj->bufSize - pObj->headerSize))
	  {
	    /* LOG and disconnect the RELCi */
	    ERRLOG "%s Receive a  body of %d greater than %d\n",
	           pObj->cnxName,
	    	   pObj->nbToRead ,
	          (pObj->bufSize - pObj->headerSize)
	    ENDERRLOG
//	    DUMPX "header", prcvMsg ,pObj->headerSize  EDUMPX
	    uma_tcp_sockDiscAssert(pObj);
	    FSM_SET(pfsm,STATE_DISCONNECTING);

	  }
          /*
	  **  put code here to call a callback fo checking the
	  **  message consistency
	  **
	  **  purely optional
	  */

          pObj->nbRead =  0;
          if (pObj->nbToRead == 0)
          {
            /*
            ** full message received
            */
            pObj->full_msg_rcv = TRUE;
            FSM_SET(pfsm,STATE_MESSAGE_RECEIVED);

          }
          /*
          **  attempt to read the payload
          */
          pObj->full_msg_rcv = FALSE;
          pObj->full_msg_rcv = uma_tcp_readSocket(pObj,pfsm);
          FSM_SET(pfsm,STATE_PAYLOAD_READING);
        }
	FSM_TRANS_END
        /*
        ** wait the next wake up: enter here when the full header
	** is not received on the first read
        */
        FSM_TRANS_BEGIN (pObj->msg_in==TRUE)

          pObj->msg_in = FALSE;
          pObj->header_rcv = uma_tcp_readSocket(pObj,pfsm);
          /*
          ** stay in the same state
          */
	FSM_TRANS_END
      FSM_STATE_END


      /*
      ** ----------------------------------
      **   STATE_PAYLOAD_READING
      **-----------------------------------
      */

      FSM_STATE_BEGIN (STATE_PAYLOAD_READING)
        FSM_TRANS_BEGIN (pObj->full_msg_rcv==TRUE)

          /*
          **  full message received
          */
          pObj->full_msg_rcv=FALSE;
          FSM_SET(pfsm,STATE_MESSAGE_RECEIVED);
        FSM_TRANS_END

          /*
          **  more byte to read
          */
        FSM_SET(pfsm,STATE_WAITPAYLOAD);
     FSM_STATE_END


      /*
      ** ----------------------------------
      **   STATE_WAITPAYLOAD
      **-----------------------------------
      */

      FSM_STATE_BEGIN (STATE_WAITPAYLOAD)
        FSM_TRANS_BEGIN (pObj->msg_in==TRUE)

          pObj->msg_in = FALSE;
          pObj->full_msg_rcv = uma_tcp_readSocket(pObj,pfsm);
          FSM_SET(pfsm,STATE_PAYLOAD_READING);
        FSM_TRANS_END
      FSM_STATE_END

      /*
      ** ----------------------------------
      **   STATE_MESSAGE_RECEIVED
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_MESSAGE_RECEIVED)
        FSM_ACTION_BEGIN
          /*
          **  full message has been receiveds
          */
          (pObj->userRcvCallBack)(pObj->userRef,pObj->relcRef,pObj->rcvBufRef);
          /*
          ** clear the buffer reference
          */
          pObj->rcvBufRef = NULL;
          pObj->stats.nbBufRcv++;
	FSM_ACTION_END
        FSM_SET(pfsm,STATE_WAIT_HEADER);
      FSM_STATE_END

      /*
      ** ----------------------------------
      **   STATE_DISCONNECTING
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_DISCONNECTING)

        FSM_ACTION_BEGIN
          /*
          **  free the current receive buffer
          */
          if (pObj->rcvBufRef != NULL) {
            ruc_buf_freeBuffer(pObj->rcvBufRef);
            pObj->rcvBufRef = NULL;
	  }
        FSM_ACTION_END
	  /*
	  **  UCT
	  */
          FSM_SET(pfsm,STATE_DISCONNECTED)
      FSM_STATE_END


      /*
      ** ----------------------------------
      **   STATE_DISCONNECTED
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_DISCONNECTED)
        /*
        ** we are dead waiting just for the TCP connection
	** to be resetted
        */

	FSM_ACTION_BEGIN
          /*
	  **  use the error callback of the user
	  */
	  pObj->rcvLock = TRUE;
	  /*
	  ** call the main in order to close the
	  ** socket and clean up the connection
	  ** with the socket controller
	  */
	  uma_tcp_disconnect(pObj);

	FSM_ACTION_END

        FSM_TRANS_BEGIN (pObj->msg_in==TRUE)
          /*
          ** nothing to do
          */
          pObj->msg_in = FALSE;
        FSM_TRANS_END

        FSM_TRANS_BEGIN (pObj->rcvRestart==TRUE)
          /*
          ** return to the current state
          */
          pObj->rcvRestart = FALSE;
	  pObj->rcvLock = FALSE;
	  pObj->nbRead =  0;
	  FSM_SET(pfsm,STATE_WAIT_HEADER)

        FSM_TRANS_END
      FSM_STATE_END

      /*
      ** ----------------------------------
      **   STATE_DELETING
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_DELETING)

        FSM_ACTION_BEGIN
          /*
          **  free the current receive buffer
          */
          if (pObj->rcvBufRef != NULL) {
            ruc_buf_freeBuffer(pObj->rcvBufRef);
            pObj->rcvBufRef = NULL;
	  }
        FSM_ACTION_END

        FSM_TRANS_BEGIN (pObj->xmitDead)
	  pObj->xmitDead = FALSE;
          FSM_SET(pfsm,STATE_DELETED);
        FSM_TRANS_END

      FSM_STATE_END

     /*
     ** ----------------------------------
     **   STATE_DELETED
     **-----------------------------------
     */
      FSM_STATE_BEGIN (STATE_DELETED)
        /*
        ** we are dead waiting just for the TCP connection
	** to be resetted
        */

	FSM_ACTION_BEGIN
          /*
	  **  use the error callback of the user
	  */
	  pObj->rcvLock = TRUE;
	  /*
	  ** call the main in order to close the
	  ** socket and clean up the connection
	  ** with the socket controller
	  */
	  uma_tcp_endOfFsmDeletion(pObj);

	FSM_ACTION_END

        FSM_TRANS_BEGIN (pObj->msg_in==TRUE)
          /*
          ** nothing to do
          */
          pObj->msg_in = FALSE;
        FSM_TRANS_END
      FSM_STATE_END

      /*
      ** ----------------------------------
      **   STATE_BUFFER_DEPLETION
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_BUFFER_DEPLETION)
       FSM_ACTION_BEGIN
       pObj->depletionCounter = UMA_TCP_BUF_DEPLETION_COUNTER;
       FSM_ACTION_END
       FSM_TRANS_BEGIN (pObj->msg_in==TRUE)
        pObj->rcvBufRef = ruc_buf_getBuffer(pObj->rcvBufHead);
        if (pObj->rcvBufRef == NULL)
        {
          /*
          ** return back to buffer depletion waiting
	  ** UMA_TCP_BUF_DEPLETION_COUNTER to become 0
	  ** before retrying to allocate a buffer
	  */
          pObj->msg_in = FALSE;
          FSM_SET(pfsm,STATE_BUFFER_DEPLETION);
        }
          /*
          ** we got a buffer return back to wait header
          ** indicates the number of bytes to read
          */
          pObj->nbToRead=pObj->headerSize;
          pObj->nbRead =  0;
          FSM_SET(pfsm,STATE_WAIT_HEADER);

       FSM_TRANS_END
     FSM_STATE_END

     default:
       ERRLOG "%s bad state %d\n", pObj->cnxName, pfsm->fsm_state ENDERRLOG
       uma_tcp_sockDiscAssert(pObj);
       FSM_SET(pfsm,STATE_DISCONNECTING);

    }
  }
}


void uma_tcp_rcvFsm_init(uma_tcp_t *ObjRef,uma_fsm_t *pfsm)
{
  uma_fsm_engine_init(ObjRef,
		      pfsm,
		      ObjRef->moduleId,
		      (exec_fsm_t)uma_tcp_rcvFsm_execute,
		      uma_tcp_rcvFsm_printHistRec,
		      (&(ObjRef->integrity)+1));


  pfsm->fsm_state = STATE_BEGIN;
  pfsm->fsm_action = TRUE;
  uma_fsm_trace(TRUE,pfsm->objPtr,pfsm);
}


