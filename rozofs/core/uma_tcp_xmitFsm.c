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
#include "ruc_sockCtl_api.h"
#include "ruc_buffer_api.h"
#include "ruc_trace_api.h"
#include "uma_fsm_framework.h"
#include "uma_tcp.h"


/*
**   D E F I N I T I O N S
*/

/*
** FSM States
*/
/*
** FSM States
*/
#define STATE_BEGIN  1
#define STATE_CURRENT 2
#define STATE_WAIT_CREDIT 3
#define STATE_SENDING 4
#define STATE_CONGESTED 5
#define STATE_UPDATE_CREDIT 6
#define STATE_END_OF_CONG 7
#define STATE_DISCONNECTED 8
#define STATE_DELETED 9





/*__________________________________________________________________________
  FUNCTION: tcp_xmitFsm_state2String
  Returns a string from a value within a list of constants
  Generated on 2005/03/25
  ==========================================================================
  PARAMETERS:
  - x: value to translate into a string
  RETURN: a string
  ==========================================================================*/
char * tcp_xmitFsm_state2String (int x) {

  switch(x) {
  case STATE_BEGIN: return "STATE_BEGIN";
  case STATE_CURRENT: return "STATE_CURRENT";
  case STATE_WAIT_CREDIT: return "STATE_WAIT_CREDIT";
  case STATE_SENDING: return "STATE_SENDING";
  case STATE_CONGESTED: return "STATE_CONGESTED";
  case STATE_UPDATE_CREDIT: return "STATE_UPDATE_CREDIT";
  case STATE_END_OF_CONG: return "STATE_END_OF_CONG";
  case STATE_DISCONNECTED: return "STATE_DISCONNECTED";
  case STATE_DELETED: return "STATE_DELETED";
    /* Value out of range */
  default: return "?unknown? ";
  }
}
/*__________________________________________________________________________
  Display an history record
  ==========================================================================
  PARAMETERS:
  . printState : whether the state is to be displayed
  RETURN: none
  ==========================================================================*/
void uma_tcp_xmitFsm_printHistRec( uint8_t printState, uint32_t flag0, uint32_t flag1) {
  int32_t  idx = 0;
  uint32_t mask = 0x80000000;

  uint8_t state = flag1 &0xFF;

  if ((flag0 == 0) && (flag1 == 0)) return;

  flag1 = flag1 & 0xFFFFFF00;

  if (printState) {
    printf ("TCP XMIT        V\n");
    printf ("TCP XMIT      %s ", tcp_xmitFsm_state2String(state));
  }
  else {
    printf ("TCP XMIT        | ");
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







void uma_tcp_updateXmitCredit(uma_tcp_t *pObj)
{
  /*
  ** if the credit reference is -1, there is
  ** full credit
  */
  if (pObj->timeCredit==(uint16_t)-1)
  {
    pObj->transmitCredit = pObj->timeCredit;
    /*return; */
  }
  if (pObj->transmitCredit != 0)
    pObj->transmitCredit--;
  /*
  ** update the credits of each queue is there are 0
  */
  if ((pObj->creditPrio0 == 0) && (pObj->creditPrio1 == 0))
  {
    pObj->creditPrio0 = RUC_RELC_XMIT_CREDIT_PRIO_0;
    pObj->creditPrio1 = RUC_RELC_XMIT_CREDIT_PRIO_1;

  }
}


void uma_tcp_eval_xmit_otherFlags(uma_tcp_t *pObj)
{

    pObj->xmitQprio0Empty = FALSE;
    pObj->xmitQprio1Empty = FALSE;
    pObj->xmitcredit = TRUE;
    pObj->xmitBufPoolEmpty = FALSE;

    pObj->xmitQprio0Empty = ruc_objIsEmptyList(&pObj->xmitList[0]);
    pObj->xmitQprio1Empty = ruc_objIsEmptyList(&pObj->xmitList[1]);
    if (pObj->transmitCredit == 0)
      pObj->xmitcredit = FALSE;
    else
      pObj->xmitcredit = TRUE;
    pObj->xmitBufPoolEmpty = ruc_buf_isPoolEmpty(pObj->xmitPoolRef);

}


/*
**  assert xmitPending
**    IN ) buffer reference
**         priority (0 ) highest, 1 lowest )
**
*/
void uma_tcp_xmitPendingAssert(uma_tcp_t *pObj,
 //64BITS                               uint32_t bufRef,
                                void *bufRef,
                                uint8_t  prio)
{
   pObj->xmitPending = TRUE;
   pObj->xmitBufRef = bufRef;
   if (pObj->prio > 1) pObj->prio = 1;
   else pObj->prio = prio;
   pObj->xmitBufRefType = UMA_XMIT_TYPE_BUFFER;

   uma_fsm_engine(pObj,&pObj->xmitFsm);
}


/*
**  assert xmitBufferReqPending
**    IN ) buffer reference
**         priority (0 ) highest, 1 lowest )
**
*/
void uma_tcp_xmitBufferReqPendingAssert(uma_tcp_t *pObj,
//64BITS                                        uint32_t bufRef,
                                        void *bufRef,
                                        uint8_t  prio)
{
   pObj->xmitPending = TRUE;
   pObj->xmitBufRef = bufRef;
   if (pObj->prio > 1) pObj->prio = 1;
   else pObj->prio = prio;
   pObj->xmitBufRefType = UMA_XMIT_TYPE_XMIT_ASSOC;
   uma_fsm_engine(pObj,&pObj->xmitFsm);
}

/*
**  assert endOfCong
*/
void uma_tcp_xmit_endOfCongAssert(uma_tcp_t *pObj)
{
   pObj->endOfCong = TRUE;
   uma_fsm_engine(pObj,&pObj->xmitFsm);
}



uint32_t uma_tcp_fsm_xmit_create(uma_tcp_t *pObj)
{
   pObj->xmitFsm.fsm_state = STATE_BEGIN;
   ruc_listHdrInit(&pObj->xmitList[0]);
   ruc_listHdrInit(&pObj->xmitList[1]);
   uma_fsm_engine(pObj,&pObj->xmitFsm);
   return (RUC_OK);
}


//64BITS uint32_t uma_tcp_xmitGetBuffer(uma_tcp_t *pObj)
void * uma_tcp_xmitGetBuffer(uma_tcp_t *pObj)
{
  uma_xmit_assoc_t *pXmitReq;
//64BITS  uint32_t  bufRef = 0;
  void  *bufRef = NULL;

  uint8_t   status = RUC_OK;



   pXmitReq = (uma_xmit_assoc_t*)pObj->xmitBufRef;
   /*
   ** now clear the reference of the buffer
   ** since the transmission is now in progress
   */
   pObj->xmitBufRef = NULL;
   /*
   ** need to allocate a buffer from the free transmit buffer pool
   */
   bufRef = ruc_buf_getBuffer(pObj->xmitPoolRef);
   status = RUC_OK;
   if (bufRef == NULL)
   {
     /*
     **  out of xmit buffer, that 's not normal
     */
     status = RUC_NOK;
   }

   /*
   ** the caller is intended to fill the buffer
   ** in any it should release that buffer
   */
   (pXmitReq->xmitCall)((uint32_t)pXmitReq->userRef,bufRef,status);

   return bufRef;

}


/*
**  Read the pending xmit list of the socket
**     IN ) xmit fsm context
**          priority index (0 or 1)
*/

//64BITS uint32_t ruc_relcXmitPendingRead(uma_tcp_t *pObj,uint8_t prio)
void * ruc_relcXmitPendingRead(uma_tcp_t *pObj,uint8_t prio)
{
  ruc_obj_desc_t *bufRef;
  uma_xmit_assoc_t *pXmitReq;
  uint8_t   opcode = 0;
  uint8_t   status = RUC_OK;

  bufRef = (ruc_obj_desc_t*)ruc_objReadQueue(&pObj->xmitList[prio],&opcode);
  if (bufRef == (ruc_obj_desc_t*) NULL)
  {
    /*
    ** there is something rotten
    */
    RUC_WARNING(-1);
    return NULL;
  }
  /*
  ** OK, now check if it is a real buffer to transmit or a transmit
  ** request for which the transmitter must allocate an xmit buffer
  */
  switch (opcode)
  {
     case UMA_XMIT_TYPE_BUFFER:
      /*
      ** nothing more to do
      */
     break;
     case UMA_XMIT_TYPE_XMIT_ASSOC:
       pXmitReq = (uma_xmit_assoc_t*)bufRef;
       /*
       ** need to allocate a buffer from the free transmit buffer pool
       */
       bufRef = (ruc_obj_desc_t*)ruc_buf_getBuffer(pObj->xmitPoolRef);
       status = RUC_OK;
       if (bufRef == (ruc_obj_desc_t*)NULL)
       {
	 /*
	 **  out of xmit buffer, that 's not normal
	 */
	 status = RUC_NOK;
       }

       /*
       ** the caller is intended to fill the buffer
       ** in any it should release that buffer
       */
       (pXmitReq->xmitCall)((uint32_t)pXmitReq->userRef,bufRef,status);
     break;
     default:
       bufRef = NULL;
     break;
  }
  return bufRef;
}


/*
**  insert a buffer in the xmit pending list of
**  the socket
*/

void ruc_relcXmitPendingInsert( uma_tcp_t *pObj)
{
   ruc_objPutQueue(&pObj->xmitList[pObj->prio],
                   (ruc_obj_desc_t*)pObj->xmitBufRef,
		    pObj->xmitBufRefType);

}

/*
**   Purge the transmit queue of the transmitter
**   both queues are purged.
*/
void ruc_relci_purgeTransmitQ(uma_tcp_t *pObj)
{
  ruc_obj_desc_t *bufRef;
  uint32_t          i;
  uint8_t           opcode = 0;
  uma_xmit_assoc_t* pXmitReq;

  for (i = 0; i <UMA_MAX_TCP_XMIT_PRIO; i++)
  {
    while ((bufRef = ruc_objReadQueue(&pObj->xmitList[i],&opcode))!=(ruc_obj_desc_t*)NULL)
    {
       /*
       ** OK, now check if it is a real buffer to transmit or a transmit
       ** request for which the transmitter must allocate an xmit buffer
       */
       switch (opcode)
       {
	  case UMA_XMIT_TYPE_BUFFER:
	   ruc_buf_freeBuffer(bufRef);
	  break;
	  case UMA_XMIT_TYPE_XMIT_ASSOC:
	    pXmitReq = (uma_xmit_assoc_t*)bufRef;

	    (pXmitReq->xmitCall)((uint32_t)pXmitReq->userRef,NULL,RUC_NOK);
	  break;
	  default:
	  break;
       }
    }
  }
}



void ruc_relci_privateSend(uma_tcp_t *pObj)
{
  int ret;
  char *pbuf;
  int len;

  /*
  **  clear the potential flags asserted by the xmit function
  */

  pObj->xmitDone = FALSE;
  pObj->xmitWouldBlock = FALSE;
  pObj->xmitErr = FALSE;
  ;
  if (pObj->xmitBufRefCur == NULL)
  {
    /*
    **  should not occur, assert xmitDone by default
    */
    RUC_WARNING(-1);
    pObj->xmitDone = TRUE;
  }
  /*
  ** get the payload and length of the transmit buffer
  ** and then adjust it according to the current number
  ** of bytes already sent
  */
  pbuf = (char*)ruc_buf_getPayload(pObj->xmitBufRefCur);
  len = ruc_buf_getPayloadLen(pObj->xmitBufRefCur);
  if (len == 0)
  {
    /*
    ** why not ?
    */
    pObj->xmitDone = TRUE;
    return;
  }
  pbuf +=pObj->nbSent;
  len -=pObj->nbSent;

  ret=send(pObj->socketRef,pbuf,len,0);
  if (ret < 0)
  {
    /*
    ** 2 cases only ) WOULD BLOCK or deconnection
    */
    if (errno==EWOULDBLOCK)
    {
      /*
      ** congestion detected) the message has not been
      ** sent
      */
      pObj->stats.nbCongested++;
      pObj->xmitWouldBlock = TRUE;
      return ;
    }
    else
    {
      pObj->xmitErr = TRUE;
      return ;
    }
  }
  /*
  ** update stats in bytes
  */
  pObj->stats.nbByteXmit +=ret;
  /*
  ** the message has been sent. Double check
  ** that everything has been sent
  */
  if (ret == len)
  {
    /*
    ** it is OK
    */
    pObj->stats.nbBufXmit++;
    pObj->xmitDone = TRUE;
    return;
  }
  /*
  ** only some bytes have been sent. It is considered as
  ** a congestion case. The number of bytes that have
  ** been sent is updated
  */
  pObj->nbSent  += (uint32_t)ret;
  pObj->stats.nbCongested++;
  pObj->xmitWouldBlock = TRUE;
  return;
}



 void uma_tcp_xmitFsm_execute(uma_tcp_t *pObj,
                      uma_fsm_t  *pfsm)
{


  while(1)
  {
    /*
    ** put code here to assert some useful flags such as the
    ** states of the xmit/receive queues
    */

    /*
    ** Global transition)if a disconnect is encountered
       the transmit queue is purged and if there is
       any buffer pending in transmit process or a buffer
       to be sent, it is released.
    */
    uma_tcp_eval_xmit_otherFlags(pObj);

    while(1)
    {
      FSM_TRANS_BEGIN (pObj->sockDiscXmit==TRUE)
	pObj->sockDiscXmit = FALSE;
	if (pObj->xmitBufRefCur != NULL)
	{
	  ruc_buf_freeBuffer(pObj->xmitBufRefCur);
	  pObj->xmitBufRefCur =  NULL;
	}
	/*
	** purge the transmit queue
	*/
	ruc_relci_purgeTransmitQ(pObj);
	/*
	** UCT to disconnected
	*/
	FSM_SET(pfsm,STATE_DISCONNECTED);
      FSM_TRANS_END
      /*
      ** test if a reset has been recieved: in that
      ** case all the resources must be released
      */

      FSM_TRANS_BEGIN (pObj->xmitReset==TRUE)
	ruc_relci_purgeTransmitQ(pObj);
	pObj->xmitReset = FALSE;
	if (pObj->xmitBufRefCur != NULL)
	{
	  ruc_buf_freeBuffer(pObj->xmitBufRefCur);
	  pObj->xmitBufRefCur =  NULL;
	}
	/*
	** FDL??? ) should not occur
	*/
	if (pObj->xmitBufRef != NULL)
	  ruc_buf_freeBuffer(pObj->xmitBufRef);

	/*
	**  add code here to stop the transmit credit
	**  timer
	*/

	/*
	** UCT to disconnected
	*/
	FSM_SET(pfsm,STATE_DELETED);
      FSM_TRANS_END

      FSM_TRANS_BEGIN (pObj->xmitRestart==TRUE)
        /*
        ** clear restart flag
        */
        pObj->xmitRestart = FALSE;
	pObj->congested = FALSE;
        /*
	**  release the current buffer
	*/
      	if (pObj->xmitBufRefCur != NULL)
	{
	  ruc_buf_freeBuffer(pObj->xmitBufRefCur);
	  pObj->xmitBufRefCur =  NULL;
	}

	/*
	** FDL??? ) should not occur
	*/
	if (pObj->xmitBufRef != NULL)
	  ruc_buf_freeBuffer(pObj->xmitBufRef);
	/*
	** return to update credit to check the content
	** of the xmit queues
	*/
	FSM_SET(pfsm,STATE_UPDATE_CREDIT)
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

	  FSM_SET(pfsm,STATE_CURRENT);
	FSM_ACTION_END
        /*
        **  UCT transition
        */
      FSM_STATE_END

      /*
      ** ----------------------------------
      **   STATE_CURRENT
      **   The transmitter is ready however
      **   is can be out of credit
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_CURRENT)
        FSM_TRANS_BEGIN ((pObj->xmitPending==TRUE) &&(pObj->transmitCredit==0))

          /*
          ** out of credit
          */
          ruc_relcXmitPendingInsert(pObj);
          pObj->xmitPending = FALSE;
          pObj->xmitBufRef =NULL;
          pObj->xmitBufRefType =0;

          /*
          ** UCT to WAIT_CREDIT
          */
          FSM_SET(pfsm,STATE_WAIT_CREDIT);
	FSM_TRANS_END

	FSM_TRANS_BEGIN((pObj->xmitPending==TRUE) &&
            (ruc_objIsEmptyList(&pObj->xmitList[0])==TRUE) &&
            (ruc_objIsEmptyList(&pObj->xmitList[1])==TRUE) &&
            (pObj->transmitCredit !=0))
	  /*
	  ** the following function is intended to return the
	  ** xmit buffer reference. It could be possible that
	  ** the xmit type is a buffer request, so in that case
	  ** the transmitter must allocate a buffer
	  */
          pObj->xmitPending = FALSE;
          if (pObj->xmitBufRefType == UMA_XMIT_TYPE_XMIT_ASSOC)
	  {
	   /*
	   ** send no buf case:
	   ** do not clear xmitBufRef here, it is done in
	   ** uma_tcp_xmitGetBuffer() before calling the user
	   ** callback
	   */
	    pObj->xmitBufRefCur = uma_tcp_xmitGetBuffer(pObj);
	  }
	  else
	  {
            pObj->xmitBufRefCur = pObj->xmitBufRef;
            pObj->xmitBufRef =NULL;
	  }
          pObj->nbSent = 0;
          ruc_relci_privateSend(pObj);
          FSM_SET(pfsm,STATE_SENDING);
	FSM_TRANS_END
      FSM_STATE_END


      /*
      ** ----------------------------------
      **   STATE_SENDING
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_SENDING)
        /*
        **  check the code returned by ruc_relci_privateSend()
        */
	FSM_TRANS_BEGIN (pObj->xmitWouldBlock)
          /*
          ** the socket is now congested
          */
	  pObj->xmitWouldBlock = FALSE;
          pObj->congested = TRUE;
          FSM_SET(pfsm,STATE_CONGESTED);
	FSM_TRANS_END

	FSM_TRANS_BEGIN (pObj->xmitErr)
          /*
          ** UCT to disconnected : it will go to there for the
	  ** main FSM transition
          */
	  pObj->xmitErr = FALSE;
	  uma_tcp_sockDiscAssert(pObj);
          FSM_SET(pfsm,STATE_DISCONNECTED);
	FSM_TRANS_END

	FSM_TRANS_BEGIN  (pObj->xmitDone)
          /*
          ** update the credit and release
          ** the transmit buffer
          */
	  pObj->xmitDone = FALSE;
          uma_tcp_updateXmitCredit(pObj);
          /*
          **  release the current buffer
	  */
          ruc_buf_freeBuffer(pObj->xmitBufRefCur);
          pObj->xmitBufRefCur = NULL;
          pObj->nbSent = 0;
          FSM_SET(pfsm,STATE_UPDATE_CREDIT);
	FSM_TRANS_END
      FSM_STATE_END


      /*
      ** ----------------------------------
      **   STATE_UPDATE_CREDIT
      **-----------------------------------
      */

      FSM_STATE_BEGIN (STATE_UPDATE_CREDIT)
        FSM_TRANS_BEGIN (
            (ruc_objIsEmptyList(&pObj->xmitList[0])==TRUE) &&
            (ruc_objIsEmptyList(&pObj->xmitList[1])==TRUE)
           )
          /*
          **  return to CURRENT
          */
          FSM_SET(pfsm,STATE_CURRENT);
	FSM_TRANS_END

        /*
        **   check the state of the highest queue
        */
        FSM_TRANS_BEGIN (
            (ruc_objIsEmptyList(&pObj->xmitList[0])==FALSE) &&
            ((pObj->creditPrio0 != 0) ||
             ((pObj->creditPrio0 == 0 )&&(ruc_objIsEmptyList(&pObj->xmitList[1])==TRUE))) &&
            (pObj->transmitCredit!=0)
           )

	  /*
          ** update the credit
          */
          if (pObj->creditPrio0 != 0) pObj->creditPrio0--;

          /*
          **  read the first buffer from the pending queue
          */
          pObj->xmitBufRefCur = ruc_relcXmitPendingRead(pObj,0);
          pObj->nbSent = 0;
          ruc_relci_privateSend(pObj);
          FSM_SET(pfsm,STATE_SENDING);
	FSM_TRANS_END

        /*
        **   check the state of the lowest queue
        */
        FSM_TRANS_BEGIN (
            (ruc_objIsEmptyList(&pObj->xmitList[1])==FALSE) &&
            ((pObj->creditPrio1 != 0)||
             ((pObj->creditPrio1 == 0 )&&(ruc_objIsEmptyList(&pObj->xmitList[0])==TRUE))) &&
            (pObj->transmitCredit!=0)
           )

	  /*
          ** update the credit
          */
          if (pObj->creditPrio1 != 0) pObj->creditPrio1--;

          /*
          **  read the first buffer from the pending queue
          */
          pObj->xmitBufRefCur = ruc_relcXmitPendingRead(pObj,1);
          pObj->nbSent = 0;
          ruc_relci_privateSend(pObj);
          FSM_SET(pfsm,STATE_SENDING);
	FSM_TRANS_END
      FSM_STATE_END



      /*
      ** ----------------------------------
      **   STATE_CONGESTED
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_CONGESTED)
      	FSM_ACTION_BEGIN
	  pObj->eocCounter = UMA_TCP_CNX_EOC_THRESHOLD;
        FSM_ACTION_END
        FSM_TRANS_BEGIN (pObj->xmitPending == TRUE)
	  /*
	  **  insert the current xmit buffer or buffer request in the
	  **  xmit pending list corresponding to its priority
	  */
          ruc_relcXmitPendingInsert(pObj);
          pObj->xmitPending = FALSE;
          pObj->xmitBufRef =NULL;
          pObj->xmitBufRefType = 0;
          /*
          ** UCT to STATE_CONGESTED
          */
        FSM_TRANS_END


        FSM_TRANS_BEGIN (pObj->endOfCong == TRUE)
          pObj->endOfCong = FALSE;
          pObj->congested = FALSE;
          FSM_SET(pfsm,STATE_END_OF_CONG);
        FSM_TRANS_END
      FSM_STATE_END

      /*
      ** ----------------------------------
      **   STATE_END_OF_CONG
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_END_OF_CONG)
        /*
        **  go back to SENDING
        */
	FSM_ACTION_BEGIN
          ruc_relci_privateSend(pObj);
          FSM_SET(pfsm,STATE_SENDING);
        FSM_ACTION_END
      FSM_STATE_END


      /*
      ** ----------------------------------
      **   STATE_DISCONNECTED
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_DISCONNECTED)

        FSM_ACTION_BEGIN
	  /*
	  ** free the transmitter
	  */
	  if (pObj->xmitBufRefCur !=  NULL)
	  {
	    ruc_buf_freeBuffer(pObj->xmitBufRefCur);
            pObj->xmitBufRefCur = NULL;
	  }
          pObj->nbSent = 0;
	FSM_ACTION_END
        FSM_TRANS_BEGIN (pObj->xmitPending==TRUE)
          /*
          ** insert the buffer in the transmit queue
          */
          ruc_relcXmitPendingInsert(pObj);
          pObj->xmitPending = FALSE;
          pObj->xmitBufRef = NULL;
	  pObj->xmitBufRefType = 0;
        FSM_TRANS_END
        FSM_TRANS_BEGIN (pObj->xmitRestart==TRUE)
          /*
          ** insert the buffer in the transmit queue
          */
          pObj->xmitRestart = FALSE;
	  /*
	  ** return to update credit to check the content
	  ** of the xmit queues
	  */
	  FSM_SET(pfsm,STATE_UPDATE_CREDIT)
        FSM_TRANS_END


      FSM_STATE_END

      /*
      ** ----------------------------------
      **   STATE_DELETED
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_DELETED)

        FSM_ACTION_BEGIN
         /*
          **  free any transmitbuffer submitted to
          **  to the FSM transmit machine
          */
          if (pObj->xmitPending==TRUE) {
            if (pObj->xmitBufRefType == UMA_XMIT_TYPE_BUFFER)
	    {
	      ruc_buf_freeBuffer(pObj->xmitBufRef);
	    }
	    else
	    {
	       uma_xmit_assoc_t *pXmitReq = (uma_xmit_assoc_t*)pObj->xmitBufRef;
	      (pXmitReq->xmitCall)((uint32_t)pXmitReq->userRef,NULL,RUC_NOK);
	    }
	  }
          pObj->xmitPending = FALSE;
          pObj->xmitBufRef = NULL;
	  pObj->xmitBufRefType = 0;

	  /*
	  ** Signal the receive FSM that the xmit FSM
	  ** is ready for deletion
	  */
          pObj->xmitDead = TRUE;
	  uma_fsm_engine(pObj,&pObj->rcvFsm);

	FSM_ACTION_END

	/*
	** No transition. We are dead
	*/

      FSM_STATE_END
      /*
      ** ----------------------------------
      **   STATE_WAIT_CREDIT
      **-----------------------------------
      */
      FSM_STATE_BEGIN (STATE_WAIT_CREDIT)
        /*
        ** wait the credit to become not null
        */
       FSM_TRANS_BEGIN (pObj->xmitPending==TRUE)

          /*
          ** insert the buffer in the transmit queue
          */
          ruc_relcXmitPendingInsert(pObj);
          pObj->xmitPending = FALSE;
          pObj->xmitBufRef = NULL;
	  pObj->xmitBufRefType = 0;
        FSM_TRANS_END

        FSM_TRANS_BEGIN (pObj->transmitCredit!=0)
          /*
          **  credit has been recovered, re-start
          **  the transmission -> go back the UPDATE CREDIT
          */
          FSM_SET(pfsm,STATE_UPDATE_CREDIT);
        FSM_TRANS_END
      FSM_STATE_END

    }
  }
}



void uma_tcp_xmitFsm_init(uma_tcp_t *ObjRef,uma_fsm_t *pfsm)
{
  uma_fsm_engine_init(ObjRef,
		      pfsm,
		      ObjRef->moduleId,
		      (exec_fsm_t)uma_tcp_xmitFsm_execute,
		      uma_tcp_xmitFsm_printHistRec,
		      (&(ObjRef->integrity)+1));


  pfsm->fsm_state = STATE_BEGIN;
  pfsm->fsm_action = TRUE;
  uma_fsm_trace(TRUE,pfsm->objPtr,pfsm);
}





