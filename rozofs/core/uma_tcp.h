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
#ifndef UMA_TCP_H
#define UMA_TCP_H

#include <stdio.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_list.h"
#include "ruc_buffer_api.h"
#include "uma_fsm_framework.h"
#include "uma_tcp_main_api.h"


extern uma_mon_txt_cbk uma_tcp_monTxtCbk;


#define UMA_TCP_TRC(name,p1,p2,p3,p4)

/*
**  size of a socket
*/
#define UMA_TCP_SOCKET_SIZE 2048*24

/*
**  xmit and receive buffer size
*/
#define UMA_TCP_BUFSIZE 2048

/*UMA_TCP_TRC
** xmit and receive buffer number
*/
#define UMA_TCP_XMIT_BUFCOUNT 256
#define UMA_TCP_RCV_BUFCOUNT  4


#define UMA_MAX_TCP_XMIT_PRIO 3
#define UMA_CNX_NAME_MAX 32



#define RUC_RELC_XMIT_CREDIT_PRIO_0  16
#define RUC_RELC_XMIT_CREDIT_PRIO_1  32

#define UMA_XMIT_TYPE_BUFFER      1
#define UMA_XMIT_TYPE_XMIT_ASSOC  2


/*
** Number of end of congestion before taking it into account
*/
#define UMA_TCP_CNX_EOC_THRESHOLD 4



/*
** Number of rcv ready loops before retrying to allocate a buffer
*/
#define UMA_TCP_BUF_DEPLETION_COUNTER 64
/*
** structure for any connection stats
*/

typedef struct _uma_socket_stats_t
{
  uint32_t  nbBufRcv;   /* number of message received */
  uint64_t  nbByteRcv;   /* number of bytes received */
  uint64_t  nbByteXmit;   /* number of bytes sent */
  uint32_t  nbBufXmit;     /* number of messages sent */
  uint32_t  nbCongested;   /* nb of congestion detected  */

} uma_socket_stats_t;

/*
**  TCP connection block structure
*/

typedef struct _uma_tcp_t {
  ruc_obj_desc_t            link;          /* To be able to chain the MS context on any list */
  uint32_t                    relcRef;         /* Index of the context*/
  uint8_t                     cnxName[UMA_CNX_NAME_MAX];       /* ascii pattern */
  uint32_t                    integrity;     /* the value of this field is incremented at
					      each MS ctx allocation */
/*
**   DO NOT MOVE THE EVENT/FLAG ARRAY: integrity field is used for giving
**   the address of the beginning of the bitfields
*/
  /*
    _______Event flags
  */
  /*
  ** receiver events
  */
  uint32_t   sockDiscRcv:1;       /* socket disconnection */
  uint32_t   msg_in:1;		/* message available from socket */
  uint32_t   header_rcv:1;	/* header receive */
  uint32_t   full_msg_rcv:1;      /* a full message is available */
  uint32_t   rcvBufPoolEmpty:1;   /* TRUE/FALSE */

  /*
  ** transmitter events
  */
   uint32_t   congested:1;        /* transmitter is congested */
   uint32_t   endOfCong:1;        /* end of congestion or transmitter ready */
   uint32_t   sockDiscXmit:1;     /* socket disconnection */
   uint32_t   xmitErr:1;          /* error on send, not a begin of congestion */
   uint32_t   xmitWouldBlock:1;   /* congestion detected */
   uint32_t   xmitDone:1;         /* successful transmission */
   uint32_t   xmitPending:1;      /* there is one xmit pending request */
   /*
   ** not used
   */
   uint32_t   xmitBufPoolEmpty:1;   /* TRUE/FALSE */
   uint32_t   xmitQprio0Empty:1;   /* TRUE/FALSE */
   uint32_t   xmitQprio1Empty:1;   /* TRUE/FALSE */
   uint32_t   xmitQprio2Empty:1;   /* TRUE/FALSE */
   uint32_t   xmitcredit:1;        /* FALSE if transmitCredit = 0 */
   uint32_t   xmitDead:1; 	 /* the transmitter is now dead */
   uint32_t   rcvLock:1;           /* the receiver is locked waiting fro xmitdead */
   uint32_t   xmitReset:1;         /* reset of the xmit FSM */
   uint32_t   rcvReset:1;		 /* reset of the rcv FSM */
   uint32_t   xmitRestart:1;       /* restart of the xmit FSM */
   uint32_t   rcvRestart:1;        /* restart of the rcv FSM */
   uint32_t   flag0_24:1;
   uint32_t   flag0_25:1;
   uint32_t   flag0_26:1;
   uint32_t   flag0_27:1;
   uint32_t   flag0_28:1;
   uint32_t   flag0_29:1;
   uint32_t   flag0_30:1;
   uint32_t   flag0_31:1;
   uint32_t   flag0_32:1;

   uint32_t   flag1_01:1;/* FLAG 1 01-08 ARE RESERVED FOR FSM STATE */
   uint32_t   flag1_02:1;/* FLAG 1 01-08 ARE RESERVED FOR FSM STATE */
   uint32_t   flag1_03:1;/* FLAG 1 01-08 ARE RESERVED FOR FSM STATE */
   uint32_t   flag1_04:1;/* FLAG 1 01-08 ARE RESERVED FOR FSM STATE */
   uint32_t   flag1_05:1;/* FLAG 1 01-08 ARE RESERVED FOR FSM STATE */
   uint32_t   flag1_06:1;/* FLAG 1 01-08 ARE RESERVED FOR FSM STATE */
   uint32_t   flag1_07:1;/* FLAG 1 01-08 ARE RESERVED FOR FSM STATE */
   uint32_t   flag1_08:1;/* FLAG 1 01-08 ARE RESERVED FOR FSM STATE */
   uint32_t   flag1_09:1;
   uint32_t   flag1_10:1;
   uint32_t   flag1_11:1;
   uint32_t   flag1_12:1;
   uint32_t   flag1_13:1;
   uint32_t   flag1_14:1;
   uint32_t   flag1_15:1;
   uint32_t   flag1_16:1;
   uint32_t   flag1_17:1;
   uint32_t   flag1_18:1;
   uint32_t   flag1_19:1;
   uint32_t   flag1_20:1;
   uint32_t   flag1_21:1;
   uint32_t   flag1_22:1;
   uint32_t   flag1_23:1;
   uint32_t   flag1_24:1;
   uint32_t   flag1_25:1;
   uint32_t   flag1_26:1;
   uint32_t   flag1_27:1;
   uint32_t   flag1_28:1;
   uint32_t   flag1_29:1;
   uint32_t   flag1_30:1;
   uint32_t   flag1_31:1;
   uint32_t   flag1_32:1;

  /*
  ** working variables of the receiver
  */
//64BITS  uint32_t  rcvBufRef;  /* reference of the current receive buffer */
  void    *rcvBufRef;  /* reference of the current receive buffer */
  uint32_t  nbRead;     /* number of byte read */
  uint32_t  nbToRead;   /* number of byte to read */

  /*
  ** working variables for transmitter
  */
   uint16_t  transmitCredit;

   uint16_t  timeCredit;
   uint32_t  timerRef;
   uint32_t  nbSent;
//64BITS    uint32_t  xmitBufRefCur;
   void    *xmitBufRefCur;
   uint8_t   moduleId;      /* module identifier for running fsm */
   uint8_t   creditPrio0;
   uint8_t   creditPrio1;

   /*
   **   interface variables
   */
   uint8_t   prio;              /* xmit priority */
   uint8_t   xmitBufRefType;    /* buffer or buffer allocated request:
                              ** #define UMA_XMIT_TYPE_BUFFER      1
			      ** #define UMA_XMIT_TYPE_XMIT_ASSOC  2
			      */
   uint8_t   eocCounter;        /* Counter of end of congestion */
   void    *xmitBufRef;        /* reference of the xmit element */
//64BITS   uint32_t  xmitBufRef;        /* reference of the xmit element */


   /*
   **  xmit/rcv stats
   */

   uma_socket_stats_t  stats;

   /*
   **  Xmit buffer pool
   */
//64BITS  uint32_t        xmitPoolOrigin;     /*current pool reference */
//64BITS  uint32_t        xmitPoolRef;        /* head of the current xmit buffer pool */
  void          *xmitPoolOrigin;     /*current pool reference */
  void         *xmitPoolRef;        /* head of the current xmit buffer pool */

   /*
   ** receiver buffer pool
   */
//64BITS  uint32_t        rcvPoolOrigin;     /*current pool reference */
//64BITS   uint32_t        rcvBufHead;
  void          *rcvPoolOrigin;     /*current pool reference */
  void          *rcvBufHead;        /* it could be either the reference of
                                   ** the user bufferv reference pool or
				   ** the default one used by the TCP
				   ** connection
				   */

  /*
  **  configuration parameters
  */
  uint32_t        headerSize;       /* size of the header to read                 */
  uint32_t        msgLenOffset;     /* offset where the message length fits       */
  uint32_t        msgLenSize;       /* size of the message length field in bytes  */
  uint32_t        bufSize;         /* length of buffer (xmit and received)        */
  uma_tcp_recvCBK_t   userRcvCallBack;   /* callback provided by the connection owner block */
  uma_tcp_discCBK_t   userDiscCallBack; /* callBack for TCP disconnection detection         */
  ruc_pf_sock_t   userRcvReadyCallBack; /* callBack for receiver ready         */

//64BITS  uint32_t        userRef;           /* user reference that must be recalled in the callbacks */
  void          *userRef;           /* user reference that must be recalled in the callbacks */
  int           socketRef;
  uint32_t        IPaddr;
//64BITS  uint32_t        connectionId;   /* reference of the socket controller */
  void        *connectionId;   /* reference of the socket controller */
  /*
  ** depletion counter
  */
  int           depletionCounter;
  /*
  **  transmitter queues
  **  3 priorities
  */
 ruc_obj_desc_t xmitList[UMA_MAX_TCP_XMIT_PRIO]; /* pending xmit list */

 /*
 **   FSM area
 */
  uma_fsm_t  xmitFsm;
  uma_fsm_t  rcvFsm;

}  uma_tcp_t;

#ifdef UMA_TCP_MAIN_C
char * tcpFlagBits0[32] = {
  "sockDiscRcv",       /* socket disconnection */
  "msg_in",		/* message available from socket */
  "header_rcv",	/* header receive */
  "full_msg_rcv",      /* a full message is available */
  "rcvBufPoolEmpty",   /* TRUE/FALSE */
   "congested",        /* transmitter is congested */
   "endOfCong",        /* end of congestion or transmitter ready */
   "sockDiscXmit",     /* socket disconnection */
   "xmitErr",          /* error on send, not a begin of congestion */
   "xmitWouldBlock",   /* congestion detected */
   "xmitDone",         /* successful transmission */
   "xmitPending",      /* there is one xmit pending request */
   "xmitBufPoolEmpty",   /* TRUE/FALSE */
   "xmitQprio0Empty",   /* TRUE/FALSE */
   "xmitQprio1Empty",   /* TRUE/FALSE */
   "xmitQprio2Empty",   /* TRUE/FALSE */
   "xmitcredit",        /* FALSE if transmitCredit = 0 */
   "xmitDead",
   "rcvLock",
   "xmitReset",
   "rcvReset",
   "xmitRestart",
   "rcvRestart",
   "flag0_24",
   "flag0_25",
   "flag0_26",
   "flag0_27",
   "flag0_28",
   "flag0_29",
   "flag0_30",
   "flag0_31",
   "flag0_32"
};
#else
extern char * tcpFlagBits0[];
#endif



   /*
   **  Prototypes
   */

   /*
   **  PRIVATE
   */

/*
**----------------------------------------
** management part
**----------------------------------------
*/
/*
** object search
*/
 uma_tcp_t *uma_tcp_getObjRef(uint32_t tcpRef);
 /*
 ** socket controller callback functions
 */
 //64BITS
uint32_t uma_tcp_rcvReady(void* tcpRef,int socketId);
uint32_t uma_tcp_rcvMsg(void* tcpRef,int socketId);
uint32_t uma_tcp_xmitReady(void* tcpRef,int socketId);
uint32_t uma_tcp_xmitEvt(void* tcpRef,int socketId);

/*
**   FSM signals assertions
*/
void uma_tcp_sockDiscAssert(uma_tcp_t *pObj);
void uma_tcp_sockResetAssert(uma_tcp_t *pObj);
void uma_tcp_sockRestartAssert(uma_tcp_t *pObj);
void  uma_tcp_fsm_relc_tcpUpAssert(uma_tcp_t *pObj);


/*
** TCP sokcet tuning typically non blocking setup
*/
uint32_t uma_tcp_tuneTcpSocket(int socketId);

/*
** function called from the receive FSM
** upon the detection of a TCP disconnection
*/
void uma_tcp_disconnect( uma_tcp_t *p);

/*
** function called from the receiver FSM upon the
** completion of the FSM deletion. That function
** release the TCP connection object
*/
void uma_tcp_endOfFsmDeletion( uma_tcp_t *p);

/*
** initialisation of a TCP connection object.
** That function is called during the module
** initialisation only.
*/
void uma_tcp_contextInit(uma_tcp_t *pObj);

/*
** that function reinitialises the context
** the only thing that is not reinitialised
** is the receive buffer pool
** That function is called upon the creation
** of a new TCP context.
*/
void uma_tcp_contextCreate(uma_tcp_t *pObj);


/*
**----------------------------------------
**   receive FSM
**----------------------------------------
*/
 /*
 **  trace functions
 */
 char * tcp_rcvFsm_state2String (int x) ;
 void uma_tcp_rcvFsm_printHistRec( uint8_t printState, uint32_t flag0, uint32_t flag1) ;

 /*
 ** miscellaneous
 */
 void uma_tcp_eval_rcv_otherFlags(uma_tcp_t *pObj);
 uint32_t uma_tcp_extract_rcvMsgLen(char *pbuf,uint32_t offset,uint32_t fieldLen);
 uint32_t uma_tcp_readSocket  (uma_tcp_t *pObj,uma_fsm_t  *pfsm);
/*
** Signals assertion
*/
uint32_t uma_tcp_rcvFsm_begin(uma_tcp_t *pObj);
void uma_tcp_rcvFsm_msgInAssert(uma_tcp_t *pObj);

/*
** FSM call and init
*/
void uma_tcp_rcvFsm_execute(uma_tcp_t *pObj,uma_fsm_t  *pfsm);
void uma_tcp_rcvFsm_init(uma_tcp_t *ObjRef,uma_fsm_t *pfsm);
/*
**----------------------------------------
** XMIT FSM
**----------------------------------------
*/
 /*
 **  trace functions
 */
char * tcp_xmitFsm_state2String (int x) ;
void uma_tcp_xmitFsm_printHistRec( uint8_t printState, uint32_t flag0, uint32_t flag1);

 /*
 ** miscellaneous
 */
void uma_tcp_updateXmitCredit(uma_tcp_t *pObj);
void uma_tcp_eval_xmit_otherFlags(uma_tcp_t *pObj);
//64BITS uint32_t uma_tcp_xmitGetBuffer(uma_tcp_t *pObj);
void *uma_tcp_xmitGetBuffer(uma_tcp_t *pObj);
//64BITS uint32_t ruc_relcXmitPendingRead(uma_tcp_t *pObj,uint8_t prio);
void *ruc_relcXmitPendingRead(uma_tcp_t *pObj,uint8_t prio);
void ruc_relcXmitPendingInsert( uma_tcp_t *pObj);
void ruc_relci_purgeTransmitQ(uma_tcp_t *pObj);
void ruc_relci_privateSend(uma_tcp_t *pObj);

/*
** Signals assertion
*/
void uma_tcp_xmitPendingAssert(uma_tcp_t *pObj,
//64BITS                                uint32_t bufRef,
                                void *bufRef,
                                uint8_t  prio);

void uma_tcp_xmitBufferReqPendingAssert(uma_tcp_t *pObj,
//64BITS                                uint32_t bufRef,
                                        void *bufRef,
                                        uint8_t  prio);

void uma_tcp_xmit_endOfCongAssert(uma_tcp_t *pObj);


/*
** FSM call and init
*/
uint32_t uma_tcp_fsm_xmit_create(uma_tcp_t *pObj);
void uma_tcp_xmitFsm_execute(uma_tcp_t *pObj,uma_fsm_t  *pfsm);
void uma_tcp_xmitFsm_init(uma_tcp_t *ObjRef,uma_fsm_t *pfsm);
uint32_t uma_tcp_rcvFsm_check_bufferDepletion(uma_tcp_t *pObj);
#endif


