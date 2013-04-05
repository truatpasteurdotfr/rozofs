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
**   I N C L U D E  F I L E S
*/

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_timer.h"
#include "ruc_timer_struct.h"
#include "ruc_trace_api.h"
#include "ruc_sockCtl_api.h"



uint32_t  ruc_timer_getIntSockIdxFromSocketId(ruc_timer_t *p,int socketId);
/*
**   G L O B A L    D A T A
*/

/*
**  Call back function for socket controller
*/
ruc_sockCallBack_t ruc_timer_callBack_InternalSock=
  {
     ruc_timer_rcvReadyInternalSock,
     ruc_timer_rcvMsgInternalSock,
     ruc_timer_xmitReadyInternalSock,
     ruc_timer_xmitEvtInternalSock
  };

ruc_timer_t   ruc_timerContext;
uint32_t timer_lock_for_debug = 0;



 uint32_t ruc_timer_trace = TRUE;



/*----------------------------------------------
**  ruc_timer_getObjRef
**----------------------------------------------
**
**  that function builds the event message and
**  sends it to the internal socket.
**
**
**  IN :
**   none
**
**  OUT :ptr to the timer context
**
**-----------------------------------------------
*/

ruc_timer_t *ruc_timer_getObjRef()
{
  return (ruc_timer_t *)&ruc_timerContext;
}


/*----------------------------------------------
**  ruc_timer_generateTicker
**----------------------------------------------
**
**  that function builds the event message and
**  sends it to the internal socket.
**
**
**  IN :
**     p : Relci object pointer
**     evt : RUC_TIMER_TICK
**
**  OUT :NONE
**
**-----------------------------------------------
*/


void ruc_timer_generateTicker(ruc_timer_t *p,uint32_t evt)
{
  int nBytes;
  ruc_timer_signalMsg_t timerSignal;

  // 64BITS   timerSignal.timerRef = (uint32_t)p;
  timerSignal.timerRef = p;
  timerSignal.signalType = RUC_TIMER_TICK;

  nBytes = send(p->internalSocket[RUC_SOC_SEND],
                (const char *)&timerSignal,
                sizeof(ruc_timer_signalMsg_t),
                0);
  if (nBytes != sizeof(ruc_timer_signalMsg_t))
  {
    /*
    **  message not sent
    */
#if 0
    RUC_WARNING(errno);
#endif
  }
}


/*----------------------------------------------
**  ruc_timer_getIntSockIdxFromSocketId
**----------------------------------------------
**
**   That function returns the internal
**   socket index associated to socketId.
**   If the socketId is not found it
**   return -1.
**
**
**  IN :
**     p : Relci object pointer
**     socketId : socket Identifier to search
**
**  OUT : -1:not found
**       <>-1: found (RUC_SOC_SEND or RUC_SOC_RECV)
**
**-----------------------------------------------
*/

uint32_t  ruc_timer_getIntSockIdxFromSocketId(ruc_timer_t *p,int socketId)
{
   int i;


   for (i = 0;i < 2;i++)
   {
     if (p->internalSocket[i]==socketId) return (uint32_t)i;
   }
   return -1;
}



/*----------------------------------------------
**  ruc_timer_rcvReadyInternalSock
**----------------------------------------------
**
**   receive ready function: only for
**   receiver socket. Nothing expected
**   on sending socket
**
**
**  IN :
**     timerRef : Relci instance index
**     socketId : socket Identifier
**
**  OUT : always TRUE for RUC_SOC_RECV
**        always FALSE for RUC_SOC_SEND
**
**-----------------------------------------------
*/
// 64BITS uint32_t ruc_timer_rcvReadyInternalSock(uint32 timerRef,int socketId)
uint32_t ruc_timer_rcvReadyInternalSock(void * timerRef,int socketId)
{
  ruc_timer_t *p;
  uint32_t      socketIdx;

  /*
  **  Get the pointer to the timer Object
  */
  p = ruc_timer_getObjRef();
  if (p == (ruc_timer_t*)NULL)
  {
    /*
    ** bad reference
    */
    RUC_WARNING(timerRef);
    return FALSE;
  }
  socketIdx = ruc_timer_getIntSockIdxFromSocketId(p,socketId);
  if (socketIdx == -1)
  {
    /*
    ** something really wrong
    */
    RUC_WARNING(p);
    return FALSE;
  }
  if (socketIdx == RUC_SOC_SEND)
    return FALSE;
  else
    return TRUE;
}

/*----------------------------------------------
**  ruc_timer_rcvMsgInternalSock
**----------------------------------------------
**
**   receive  function: only for
**   receiver socket. Nothing expected
**   on sending socket. It indicates
**   that there is an internal message
**   pending for the Relci instance
**
**
**  IN :
**     timerRef : Relci instance index
**     socketId : socket Identifier
**
**  OUT : always TRUE for RUC_SOC_RECV
**        always FALSE for RUC_SOC_SEND
**
**-----------------------------------------------
*/
// 64BITS uint32_t ruc_timer_rcvMsgInternalSock(uint32 timerRef,int socketId)
uint32_t ruc_timer_rcvMsgInternalSock(void * timerRef,int socketId)
{
  ruc_timer_t *p;
  uint32_t      socketIdx;
  int         bytesRcvd;
  ruc_timer_signalMsg_t timerSignal;

  /*
  **  Get the pointer to the timer Object
  */
  p = ruc_timer_getObjRef();
  if (p == (ruc_timer_t*)NULL)
  {
    /*
    ** bad reference
    */
    RUC_WARNING(timerRef);
    return FALSE;
  }
  socketIdx = ruc_timer_getIntSockIdxFromSocketId(p,socketId);
  if (socketIdx == -1)
  {
    /*
    ** something really wrong
    */
    RUC_WARNING(p);
    return FALSE;
  }

  if (socketIdx == RUC_SOC_SEND)
  {
    /*
    **  should not occur
    */
    RUC_WARNING(socketId);
    return FALSE;
  }
  /*
  **  Ticker received
  **
  */
  bytesRcvd = recv(socketId,
                   (char *)&timerSignal,
                   sizeof(ruc_timer_signalMsg_t),
                   0);
  if (bytesRcvd != sizeof(ruc_timer_signalMsg_t))
  {
    /*
    **  something wrong : (BUG)
    */
    RUC_TIMER_TRC("timer_rcvMsgIntSock_err",-1,errno,socketId,-1);
    RUC_WARNING(errno);
    return TRUE;
  }
  /*
  **  process the signal
  */
  switch ( timerSignal.signalType)
  {
    case RUC_TIMER_TICK:
      ruc_timer_tickReceived(p);
      break;
    default:
      RUC_TIMER_TRC("timer_rcvMsgIntSock_err",-1,timerSignal.signalType,-1,-1);
      RUC_WARNING(timerSignal.signalType);
      break;
  }

  return TRUE;
}


/*----------------------------------------------
**  ruc_timer_xmitReadyInternalSock
**----------------------------------------------
**
**   xmit ready function: only for
**   xmit socket. Nothing expected
**   on receiving socket.
**
**
**  IN :
**     timerRef : Relci instance index
**     socketId : socket Identifier
**
**  OUT : always FALSE for RUC_SOC_RECV
**        always FALSE for RUC_SOC_SEND
**  There is not congestion on the internal socket
**
**-----------------------------------------------
*/
// 64BITS uint32_t ruc_timer_xmitReadyInternalSock(uint32 timerRef,int socketId)
uint32_t ruc_timer_xmitReadyInternalSock(void * timerRef,int socketId)
{
  ruc_timer_t *p;
  uint32_t      socketIdx;

  /*
  **  Get the pointer to the timer Object
  */
  p = ruc_timer_getObjRef();
  if (p == (ruc_timer_t*)NULL)
  {
    /*
    ** bad reference
    */
    RUC_WARNING(timerRef);
    return FALSE;
  }
  socketIdx = ruc_timer_getIntSockIdxFromSocketId(p,socketId);
  if (socketIdx == -1)
  {
    /*
    ** something really wrong
    */
    RUC_WARNING(p);
    return FALSE;
  }

  if (socketIdx == RUC_SOC_RECV)
    return FALSE;
  else
    return FALSE;
}


/*----------------------------------------------
**  ruc_timer_xmitEvtInternalSock
**----------------------------------------------
**
**   xmit event  function: only for
**   xmit socket.
**   That function should never be encountered
**
**
**  IN :
**     intSockRef : either RUC_SOC_SEND or
**                         RUC_SOC_RECV
**     socketId : socket Identifier
**
**  OUT : always FALSE for RUC_SOC_RECV
**        always TRUE for RUC_SOC_SEND
**
**-----------------------------------------------
*/
// 64BITS uint32_t ruc_timer_xmitEvtInternalSock(uint32 timerRef,int socketId)
uint32_t ruc_timer_xmitEvtInternalSock(void * timerRef,int socketId)
{
  ruc_timer_t *p;

  /*
  **  Get the pointer to the timer Object
  */
  p = ruc_timer_getObjRef();
  if (p == (ruc_timer_t*)NULL)
  {
    /*
    ** bad reference
    */
    RUC_WARNING(timerRef);
    return FALSE;
  }
  RUC_WARNING(p);
  return FALSE;
}
/*
**    I N T E R N A L   S O C K E T
**    CREATION/DELETION
*/


/*----------------------------------------------
**  ruc_timer_createInternalSocket (private)
**----------------------------------------------
**
**  That function is intented to create a
**  socket pair. That socket pair is used
**  for sending internal event back to the
**  timer instance
**
**  IN :
**     p : pointer to the timer instance
**
**  OUT :
**    RUC_OK : all is fine
**    RUC_NOK : unable to create the internal
**              socket.
**
**  note : the socket is configured as an asynchronous
**         socket.(sending socket only)
**-----------------------------------------------
*/

uint32_t ruc_timer_createInternalSocket(ruc_timer_t *p)
{
  int    ret;
  uint32_t retcode = RUC_NOK;
  int    fileflags;


  /*
  **  1 - create the socket pair
  */

  ret = socketpair(  AF_UNIX,
                  SOCK_DGRAM,
                  0,
                  &p->internalSocket[0]);

  if (ret < 0)
  {
    /*
    ** unable to create the sockets
    */
    RUC_WARNING(errno);
    return RUC_NOK;
  }
  while (1)
  {
    /*
    ** change socket mode to asynchronous
    */
    if((fileflags=fcntl(p->internalSocket[RUC_SOC_SEND],F_GETFL,0))==-1)
    {
      RUC_WARNING(errno);
      break;
    }
    if(fcntl(p->internalSocket[RUC_SOC_SEND],F_SETFL,fileflags|O_NDELAY)==-1)
    {
      RUC_WARNING(errno);
      break;
    }
    /*
    ** 2 - perform the connection with the socket controller
    */
    p->intSockconnectionId[RUC_SOC_SEND]=
                 ruc_sockctl_connect(p->internalSocket[RUC_SOC_SEND],
                                     "TMR_SOCK_XMIT",
                                      0,
				     // 64BITS (uint32_t)p,
                                      p,
                                      &ruc_timer_callBack_InternalSock);
    // 64BITS     if (p->intSockconnectionId[RUC_SOC_SEND]== (uint32_t)NULL)
    if (p->intSockconnectionId[RUC_SOC_SEND]== NULL)
    {
      RUC_WARNING(RUC_SOC_SEND);
      break;
    }
    p->intSockconnectionId[RUC_SOC_RECV]=
                 ruc_sockctl_connect(p->internalSocket[RUC_SOC_RECV],
                                     "TMR_SOCK_RECV",
                                      0,
				     // 64BITS (uint32_t)p,
                                      p,
                                      &ruc_timer_callBack_InternalSock);
    // 64BITS     if (p->intSockconnectionId[RUC_SOC_RECV]== (uint32_t) NULL)
    if (p->intSockconnectionId[RUC_SOC_RECV]== NULL)
    {
      RUC_WARNING(RUC_SOC_SEND);
      break;
    }
    /*
    **  done
    */
    retcode = RUC_OK;
    break;

  }
  if (retcode != RUC_OK)
  {
    /*
    ** something wrong: close the sockets and disconnect
    **  from socket controller
    */
    close (p->internalSocket[RUC_SOC_SEND]);
    close (p->internalSocket[RUC_SOC_RECV]);
    // 64BITS     if (p->intSockconnectionId[RUC_SOC_RECV] != (uint32_t) NULL)
    if (p->intSockconnectionId[RUC_SOC_RECV] != NULL)
    {
      ruc_sockctl_disconnect(p->intSockconnectionId[RUC_SOC_RECV]);
      // 64BITS       p->intSockconnectionId[RUC_SOC_RECV] = (uint32_t) NULL;
      p->intSockconnectionId[RUC_SOC_RECV] = NULL;
    }
    // 64BITS     if (p->intSockconnectionId[RUC_SOC_SEND] != (uint32_t) NULL)
    if (p->intSockconnectionId[RUC_SOC_SEND] != NULL)
    {
      ruc_sockctl_disconnect(p->intSockconnectionId[RUC_SOC_SEND]);
      // 64BITS       p->intSockconnectionId[RUC_SOC_SEND] = (uint32_t) NULL;
      p->intSockconnectionId[RUC_SOC_SEND] = NULL;
    }
    return RUC_NOK;
  }
  return RUC_OK;
}


/*----------------------------------------------
**  ruc_timer_deleteInternalSocket (private)
**----------------------------------------------
**
** That function is called when a Recli
** instance is deleted:
**
**   That function performs:
**    -  the closing of the socket pair
**    -  the socket controller disconnection
**    -  the purge of the signal queue list
**
**
**  IN :
**     p : pointer to the timer instance
**
**  OUT :
**    RUC_OK : all is fine
**    RUC_NOK :
**-----------------------------------------------
*/

uint32_t ruc_timer_deleteInternalSocket(ruc_timer_t *p)
{

  if (p->internalSocket[RUC_SOC_SEND] != -1)
  {
    close (p->internalSocket[RUC_SOC_SEND]);
    p->internalSocket[RUC_SOC_SEND] = -1;
  }
  if (p->internalSocket[RUC_SOC_RECV] != -1)
  {
    close (p->internalSocket[RUC_SOC_RECV]);
    p->internalSocket[RUC_SOC_SEND] = -1;
  }
  // 64BITS   if (p->intSockconnectionId[RUC_SOC_RECV] != (uint32_t) NULL)
  if (p->intSockconnectionId[RUC_SOC_RECV] != NULL)
  {
    ruc_sockctl_disconnect(p->intSockconnectionId[RUC_SOC_RECV]);
    // 64BITS     p->intSockconnectionId[RUC_SOC_RECV] = (uint32_t) NULL;
    p->intSockconnectionId[RUC_SOC_RECV] = NULL;
  }
  // 64BITS   if (p->intSockconnectionId[RUC_SOC_SEND] != (uint32_t) NULL)
  if (p->intSockconnectionId[RUC_SOC_SEND] != NULL)
  {
    ruc_sockctl_disconnect(p->intSockconnectionId[RUC_SOC_SEND]);
    // 64BITS     p->intSockconnectionId[RUC_SOC_SEND] = (uint32_t) NULL;
    p->intSockconnectionId[RUC_SOC_SEND] = NULL;
  }
  p->active = TRUE;

  return RUC_OK;
}



/*
**   P D P   C O N T E X T   S I G N A L    A P I
*/




/*----------------------------------------------
**  ruc_timer_tickReceived (private)
**----------------------------------------------
**
**  That function processes all the PDP context
**  that have been queued in the signal Queue
**  of the Relci object.
**
** note:
**  The function process up to 16 events before
**  leaving. The remaining of the queue will
**  be processed on the next select
**
**  IN :
**     p :timer module context pointer
**
**  OUT :
**    NONE
**
**-----------------------------------------------
*/

void ruc_timer_tickReceived(ruc_timer_t *p)
{

#if 0
  RUC_TIMER_TRC("timer_tickReceived",-1,-1,-1,-1);
#endif

  p->active = TRUE;
  ruc_timer_process();
  p->active = FALSE;


}



/*----------------------------------------------
**  ruc_timer_threadCreate
**----------------------------------------------
**
**
**  IN :
**
**  OUT :
**    RUC_OK/RUC_NOK
**
**-----------------------------------------------
*/

uint32_t ruc_timer_threadCreate( pthread_t  *thrdId ,
                               void         *arg )
{
  pthread_attr_t      attr;
  int                 err;

  /*
  ** The thread is initialized with
  ** attributes object default values
  */
  if((err=pthread_attr_init(&attr)) != 0)
  {
    RUC_WARNING(errno);
   return RUC_NOK;
  }
  /*
  ** Create the thread
  */
  if((err=pthread_create(thrdId,&attr,ruc_timer_TickerThread,arg)) != 0)
  {
    RUC_WARNING(errno);
    return RUC_NOK;

  }
  return RUC_OK;
}


/*----------------------------------------------
**  ruc_timer_moduleInit (public)
**----------------------------------------------
**
**
**  IN :
**     active : TRUE/FALSE
**
**  OUT :
**    RUC_OK/RUC_NOK
**
**-----------------------------------------------
*/


uint32_t ruc_timer_moduleInit(uint32_t active)

{
  ruc_timer_t *p;
  uint32_t      ret;
  /*
  **  Get the pointer to the timer Object
  */
  p = ruc_timer_getObjRef();
  p->active = active;

   /*
   ** create the timer data structure
   */
    ruc_timer_init (TIMER_TICK_VALUE_100MS,64*2);
   /*
   ** create the internal socket
   */

   ret = ruc_timer_createInternalSocket(p);
   if (ret != RUC_OK)
     return ret;

   /*
   ** create the timer thread
   */

   ret = ruc_timer_threadCreate(&p->thrdId,(void*)NULL);
   return ret;
}




/*
**   T I C K E R   T H R E A D
*/


 struct timespec  ruc_ticker;

void *ruc_timer_TickerThread(void *arg)
{

  ruc_timer_t *p;
  int count = 0;


  p = ruc_timer_getObjRef();
  /*
  **  1 second ticker
  */
 while(1)
 {
   ruc_ticker.tv_sec=0;
   ruc_ticker.tv_nsec=TIMER_TICK_VALUE_100MS*1000*1000;
   nanosleep(&ruc_ticker,(struct timespec *)NULL);
   
   if (timer_lock_for_debug) continue;
   
    if (p->active == FALSE)
    {
      /*
      ** signal the tick to the main thread
      */
      ruc_timer_generateTicker(p,0 /* N.S */);
    }
    count++;
    if (count == 4*5)
    {
      count = 0;
    }
  }
}
