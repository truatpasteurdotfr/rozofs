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
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>     
#include <pthread.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>

#include "ruc_list.h"
#include "socketCtrl.h"
#include "uma_dbg_api.h"

#define MICROLONG(time) ((unsigned long long)time.tv_sec * 1000000 + time.tv_usec)
#define RUC_SOCKCTRL_DEBUG_TOPIC      "cpu"


/*
**  G L O B A L   D A T A
*/

/*
**  priority table
*/
ruc_obj_desc_t  ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO];
/*
** head of the free connection list
*/
ruc_sockObj_t   *ruc_sockCtl_freeListHead= (ruc_sockObj_t*)NULL;
ruc_sockObj_t   *ruc_sockCtrl_pFirstCtx = (ruc_sockObj_t*)NULL;
uint32_t          ruc_sockCtrl_maxConnection = 0;
/*
** file descriptor for receiving and transmitting events
*/
fd_set  rucRdFdSet;   
fd_set  rucWrFdSet;   
/*
**  gloabl data used in the loops that polls the bitfields
*/

ruc_obj_desc_t *ruc_sockctl_pnextCur;
int   ruc_sockctl_prioIdxCur;




uint32_t ruc_sockCtrl_lastCpuScheduler = 0;
uint32_t ruc_sockCtrl_cumulatedCpuScheduler = 0;
uint32_t ruc_sockCtrl_nbTimesScheduler = 0;

uint32_t ruc_sockCtrl_looptime = 0;
uint32_t ruc_sockCtrl_looptimeMax = 0;

ruc_scheduler_t ruc_applicative_traffic_shaper = NULL;
ruc_scheduler_t ruc_applicative_poller = NULL;



static char    myBuf[UMA_DBG_MAX_SEND_SIZE];
/*
**  F U N C T I O N S
*/

/*
**   D E B U G 
*/



/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void ruc_sockCtrl_debug_show(uint32_t tcpRef, void * bufRef) {
  ruc_sockObj_t     *p;
  int                i;
  char           *pChar=myBuf;
  uint32_t          average;

  p = ruc_sockCtrl_pFirstCtx;
  pChar += sprintf(pChar,"select max cpu time : %u us\n",ruc_sockCtrl_looptimeMax);
  ruc_sockCtrl_looptimeMax = 0;   
  pChar += sprintf(pChar,"%-32s %4s %10s %10s %10s %10s\n","application","sock", "last","cumulated", "activation", "average");
  pChar += sprintf(pChar,"%-32s %4s %10s %10s %10s %10s\n\n","name","nb", "cpu","cpu","times","cpu");
  
  for (i = 0; i < ruc_sockCtrl_maxConnection; i++)
  {
    if (p->socketId !=(uint32_t)-1)
    {
      if (p->nbTimes == 0) average = 0;
      else                 average = p->cumulatedTime/p->nbTimes;
      pChar += sprintf(pChar, "%-32s %4d %10u %10u %10u %10u\n", &p->name[0],p->socketId, p->lastTime, p->cumulatedTime,p->nbTimes, average);
      p->cumulatedTime = 0;
      p->nbTimes = 0;
    }
    p++;
  }

  if (ruc_sockCtrl_nbTimesScheduler == 0) average = 0;
  else                                    average = ruc_sockCtrl_cumulatedCpuScheduler/ruc_sockCtrl_nbTimesScheduler;
  pChar += sprintf(pChar,"%-32s %4d %10u %10u %10u %10u\n", "scheduler", 0,  
		   ruc_sockCtrl_lastCpuScheduler, ruc_sockCtrl_cumulatedCpuScheduler, ruc_sockCtrl_nbTimesScheduler, average);
  ruc_sockCtrl_cumulatedCpuScheduler = 0;
  ruc_sockCtrl_nbTimesScheduler = 0;
  uma_dbg_send(tcpRef,bufRef,TRUE,myBuf);

}
/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void ruc_sockCtrl_debug(char * argv[], uint32_t tcpRef, void * bufRef) {
  ruc_sockCtrl_debug_show(tcpRef,bufRef);
}




/*__________________________________________________________________________
  Register to the debug SWBB
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void ruc_sockCtrl_debug_init() {
  uma_dbg_addTopic(RUC_SOCKCTRL_DEBUG_TOPIC, ruc_sockCtrl_debug); 
}




/*
**  END OF DEBUG
*/


#if 0
/*__________________________________________________________________________
  Trace level debug function
  ==========================================================================
  PARAMETERS: 
  - 
  RETURN: none
  ==========================================================================*/
void ruc_sockCtrl_debug_show(char *myBuf) {
  ruc_sockObj_t     *p;
  int                i;
  char           *pChar=myBuf;
  uint32_t          average;

  p = ruc_sockCtrl_pFirstCtx;
  pChar += sprintf(pChar,"select max cpu time : %u us\n",ruc_sockCtrl_looptimeMax);
  ruc_sockCtrl_looptimeMax = 0;   
  pChar += sprintf(pChar,"%-18s %4s %5s %10s %10s %10s\n","application","sock", "last","cumulated", "activation", "average");
  pChar += sprintf(pChar,"%-18s %4s %5s %10s %10s %10s\n\n","name","nb", "cpu","cpu","times","cpu");
  
  for (i = 0; i < ruc_sockCtrl_maxConnection; i++)
  {
    if (p->socketId !=(uint32_t)-1)
    {
      if (p->nbTimes == 0) average = 0;
      else                 average = p->cumulatedTime/p->nbTimes;
      pChar += sprintf(pChar, "%-20s %2d %5u %10u %10u %10u\n", &p->name[0],p->socketId, p->lastTime, p->cumulatedTime,p->nbTimes, average);
#if 0
      if (p->nbTimesXmit == 0) average = 0;
      else                 average = p->cumulatedTimeXmit/p->nbTimesXmit;
      pChar += sprintf(pChar, "%-20s    %5u %10u %10u %10u\n", "  ",p->lastTimeXmit, p->cumulatedTimeXmit,p->nbTimesXmit, average);
#endif	  
      p->cumulatedTime = 0;
      p->nbTimes = 0;
    }
    p++;
  }

}

#endif



void ruc_sockctl_updatePnextCur(ruc_obj_desc_t *pHead,
                                   ruc_sockObj_t *pobj)
{


   if (ruc_sockctl_pnextCur != (ruc_obj_desc_t*)pobj)
   {
     /*
     ** nothing to do
     */
     return;
   }
   /*
   ** ruc_sockctl_pnextCur needs to be updated
   */
   ruc_objGetNext(pHead, &ruc_sockctl_pnextCur);
     
}


/* #STARTDOC
**
**  #TITLE
uint32_t ruc_sockctl_init(uint32_t maxConnection)

**  #SYNOPSIS
**    creation of the socket controller distributor
**
**
**   IN:
**       maxConnection : number of elements to create
**
**   OUT :
**       RUC_OK: the distributor has been created
**       RUC_NOK : out of memory
**
**
** ##ENDDOC
*/

uint32_t ruc_sockctl_init(uint32_t maxConnection)
{
  int i,idx;
  ruc_obj_desc_t  *pnext=(ruc_obj_desc_t*)NULL;
  ruc_sockObj_t  *p;


  /*
  ** initialization of the priority table
  */
  for (i=0;i < RUC_SOCKCTL_MAXPRIO; i++)
  {
     ruc_listHdrInit(&ruc_sockCtl_tabPrio[i]);
  }

  /*
  ** create the connection distributor
  */
  ruc_sockCtl_freeListHead = 
              (ruc_sockObj_t *)ruc_listCreate(maxConnection,
                                              sizeof(ruc_sockObj_t));
  if (ruc_sockCtl_freeListHead == (ruc_sockObj_t*)NULL)
  {
    /*
    ** out of memory
    */
    return RUC_NOK;
  }
  /*
  **  initialize each element of the free list
  */
  idx = 0;
  while ((p = (ruc_sockObj_t*)
              ruc_objGetNext((ruc_obj_desc_t*)ruc_sockCtl_freeListHead,
                             &pnext))!=(ruc_sockObj_t*)NULL) 
  {
    p->connectId = idx;
    p->socketId = -1;
    p->priority  = -1;
    // 64BITS     p->objRef = -1;
    p->objRef = NULL;
    p->rcvCount = 0;
    p->xmitCount = 0;
    p->name[0] = 0;
    p->lastTime = 0;
    p->cumulatedTime = 0;
    p->nbTimes = 0;
    p->lastTimeXmit = 0;
    p->cumulatedTimeXmit = 0;
    p->nbTimesXmit = 0;
    p->callBack = (ruc_sockCallBack_t*)NULL;
    idx +=1;

  }

  /*
  **  save the pointer to the first context of the list
  */
  ruc_sockCtrl_pFirstCtx = (ruc_sockObj_t*)ruc_objGetFirst((ruc_obj_desc_t*)ruc_sockCtl_freeListHead);

  /*
  ** do the connection with the debug
  */
  ruc_sockCtrl_debug_init();
  
  ruc_sockCtrl_maxConnection = maxConnection;

  
  return RUC_OK;

}


/* #STARTDOC
**
**  #TITLE
uint32_t ruc_sockctl_connect(int socketId,
                           char *name;
                           uint32_t priority,
                           uint32_t objRef,
                           ruc_sockCallBack_t *callback);
**  #SYNOPSIS
**    creation of connection with the socket controller.
**    if there is a free connection entry, it returns RUC_OK
**
**
**   IN:
**       socketId : socket identifier returned by the socket() service
**       priority : polling priority
**       objRef : object reference provided as a callback parameter
**      *callBack : pointer to the call back functions.
**
**   OUT :
**       !=NULL : connection identifier
**       ==NULL : out of context
**
**
** ##ENDDOC
*/

// 64BITS uint32_t ruc_sockctl_connect(int socketId,
void * ruc_sockctl_connect(int socketId,
                           char *name,
                           uint32_t priority,
			   // 64BITS                            uint32_t objRef,
                           void *objRef,
                           ruc_sockCallBack_t *callback)
{

    ruc_sockObj_t *p,*pelem;
    uint32_t curPrio;

  /*
  ** get the first element from the free list
  */


  p = (ruc_sockObj_t*)ruc_sockCtl_freeListHead;
  pelem = (ruc_sockObj_t*)ruc_objGetFirst((ruc_obj_desc_t*)p);
  if (pelem == (ruc_sockObj_t* )NULL)
  {
    // 64BITS     return (uint32_t) NULL;
    return NULL;
  }
  /*
  **  remove the context from the free list
  */
  ruc_objRemove((ruc_obj_desc_t*)pelem);
  /*
  **  store the callback pointer,socket Id and objRef
  */
  pelem->socketId = socketId;
  
  pelem->objRef = objRef;
  pelem->rcvCount = 0;
  pelem->xmitCount = 0;
  bcopy((const char *)name, (char*)&pelem->name[0],RUC_SOCK_MAX_NAME);
  pelem->name[RUC_SOCK_MAX_NAME-1] = 0;
  pelem->callBack = callback;
  pelem->lastTime = 0;
  pelem->cumulatedTime = 0;
  pelem->nbTimes = 0; 
  pelem->lastTimeXmit = 0;
  pelem->cumulatedTimeXmit = 0;
  pelem->nbTimesXmit = 0;   
  /*
  **  insert in the associated priority list
  */
  if (priority >= RUC_SOCKCTL_MAXPRIO) 
    curPrio = RUC_SOCKCTL_MAXPRIO-1;
  else 
    curPrio = priority;
  pelem->priority  = curPrio;

  ruc_objInsert(&ruc_sockCtl_tabPrio[curPrio],(ruc_obj_desc_t*)pelem);

  // 64BITS   return ((uint32_t)pelem);
  return (pelem);

}




/* #STARTDOC
**
**  #TITLE
uint32_t ruc_sockctl_disconnect(uint32_t connectionId);
**  #SYNOPSIS
**    deletion of connection with the socket controller.
**
**
**   IN:
**       connectionId : reference returned by the connection service
**
**   OUT :
**       RUC_OK : connection identifier
**
**
** ##ENDDOC
*/

// 64BITS uint32_t ruc_sockctl_disconnect(uint32_t connectionId)
uint32_t ruc_sockctl_disconnect(void * connectionId)

{
   ruc_sockObj_t *p;
 
   p = (ruc_sockObj_t*)connectionId;

   /*
   ** update PnextCur before remove the object
   */

   ruc_sockctl_updatePnextCur(&ruc_sockCtl_tabPrio[p->priority],p);

   /*
   **  remove from the priority list
   */
   ruc_objRemove((ruc_obj_desc_t *)p);

   /*
   **  set it free
   */
   p->objRef = (void*)-1;
   if (p->socketId != -1)
   {
     /*
     ** clear the correspond bit on xmit and rcv ready
     */
     FD_CLR(p->socketId,&rucRdFdSet);     
     FD_CLR(p->socketId,&rucWrFdSet);
     p->socketId = -1;
   }

   /*
   **  insert in the free list
   */
  ruc_objInsert((ruc_obj_desc_t*)ruc_sockCtl_freeListHead,
                (ruc_obj_desc_t*)p);

  return RUC_OK;
}


/* #STARTDOC
**
**  #TITLE
void ruc_sockCtl_checkRcvBits()
**  #SYNOPSIS
**    That function check the receive bit for each active
**    connection. If the bit is set the receive function (provided
**    as call-back function)
**
**
**   IN:
**       none
**   OUT :
**       none
**
**
** ##ENDDOC
*/

void ruc_sockCtl_checkRcvBits()
{

  int i;
  ruc_sockObj_t *p;
  ruc_sockCallBack_t *pcallBack;
  int socketId;
  struct timeval     timeDay;
  unsigned long long timeBefore, timeAfter;
  
  timeBefore = 0;
  timeAfter  = 0;

  for (i = 0; i <RUC_SOCKCTL_MAXPRIO ; i++)
  {
    ruc_sockctl_pnextCur = (ruc_obj_desc_t*)NULL;
    ruc_sockctl_prioIdxCur = RUC_SOCKCTL_MAXPRIO-1-i;

    while ((p = (ruc_sockObj_t*)
              ruc_objGetNext((ruc_obj_desc_t*)&ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO-1-i],
                             &ruc_sockctl_pnextCur))!=(ruc_sockObj_t*)NULL) 
    {
#if 0
      printf("CheckRcvBits :socketId %d, name: %s\n",p->socketId,&p->name[0]);
#endif
      socketId = p->socketId;     
      if(FD_ISSET(socketId, &rucRdFdSet))
      {
        /*
        ** the receive bit is set, call the related function
        ** and update rcv count for statistics purpose
        */
        p->rcvCount++;
        pcallBack = p->callBack;

	 gettimeofday(&timeDay,(struct timezone *)0);  
	 timeBefore = MICROLONG(timeDay);
	 
        (*(pcallBack->msgInFunc))(p->objRef,p->socketId);
         gettimeofday(&timeDay,(struct timezone *)0);  
	 timeAfter = MICROLONG(timeDay);
	 p->lastTime = (uint32_t)(timeAfter - timeBefore);
	 p->cumulatedTime += p->lastTime;
	 p->nbTimes ++;
	/*
	**  clear the corresponding bit
	*/
	FD_CLR(socketId,&rucRdFdSet);
      }
    }
  }
}


static inline void ruc_sockCtl_checkRcvAndXmitBits(int nbrSelect)
{

  int i;
  ruc_sockObj_t *p;
  ruc_sockCallBack_t *pcallBack;
  int socketId;
  struct timeval     timeDay;
  unsigned long long timeBefore, timeAfter;
  int loopcount;
  
  timeBefore = 0;
  timeAfter  = 0;
  
  gettimeofday(&timeDay,(struct timezone *)0);  
  timeAfter = MICROLONG(timeDay);
  
  loopcount= nbrSelect;
  for (i = 0; i <RUC_SOCKCTL_MAXPRIO ; i++)
  {
    ruc_sockctl_pnextCur = (ruc_obj_desc_t*)NULL;
    ruc_sockctl_prioIdxCur = RUC_SOCKCTL_MAXPRIO-1-i;

    while ((p = (ruc_sockObj_t*)
              ruc_objGetNext((ruc_obj_desc_t*)&ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO-1-i],
                             &ruc_sockctl_pnextCur))!=(ruc_sockObj_t*)NULL) 
    {
#if 0
      printf("CheckRcvBits :socketId %d, name: %s\n",p->socketId,&p->name[0]);
#endif
      socketId = p->socketId;
      /*
      ** check the traffic shaper
      */
      if (ruc_applicative_traffic_shaper != NULL)
      {
       (*ruc_applicative_traffic_shaper)(timeAfter);
      }
      if (ruc_applicative_poller != NULL)
      {
       (*ruc_applicative_poller)(0);
      }
      if(FD_ISSET(socketId, &rucRdFdSet))
      {
        /*
        ** the receive bit is set, call the related function
        ** and update rcv count for statistics purpose
        */
        p->rcvCount++;
        pcallBack = p->callBack;

        gettimeofday(&timeDay,(struct timezone *)0);  
        timeBefore = MICROLONG(timeDay);

        (*(pcallBack->msgInFunc))(p->objRef,p->socketId);
        gettimeofday(&timeDay,(struct timezone *)0);  
        timeAfter = MICROLONG(timeDay);
        p->lastTime = (uint32_t)(timeAfter - timeBefore);
        p->cumulatedTime += p->lastTime;
        p->nbTimes ++;
        /*
        **  clear the corresponding bit
        */
        FD_CLR(socketId,&rucRdFdSet);
        loopcount--;
        if (loopcount == 0) break;
      }
      if(FD_ISSET(socketId, &rucWrFdSet))
      {
        /*
        ** the receive bit is set, call the related function
        ** and update rcv count for statistics purpose
        */
        p->xmitCount++;
        pcallBack = p->callBack;

        gettimeofday(&timeDay,(struct timezone *)0);  
        timeBefore = MICROLONG(timeDay);

        (*(pcallBack->xmitEvtFunc))(p->objRef,p->socketId);

        timeAfter = MICROLONG(timeDay);
        p->lastTime = (uint32_t)(timeAfter - timeBefore);
        p->cumulatedTime += p->lastTime;
        p->nbTimes ++;
#if 0
        p->lastTimeXmit = (uint32_t)(timeAfter - timeBefore);
        p->cumulatedTimeXmit += p->lastTimeXmit;
        p->nbTimesXmit ++;
#endif
        /*
        **  clear the corresponding bit
        */
	FD_CLR(socketId,&rucWrFdSet);
        loopcount--;
        if (loopcount == 0) break;
      }
    }
  }
}


/* #STARTDOC
**
**  #TITLE
void ruc_sockCtl_prepareRcvBits()
**  #SYNOPSIS
**    That function calls the receiverready function of
**    each active connection. If the application replies
**    TRUE then the corresponding bit is set.
**
**   IN:
**       none
**   OUT :
**       none
**
**
** ##ENDDOC
*/


void ruc_sockCtl_prepareRcvBits()
{

  int i;
  ruc_sockObj_t *p;
  uint32_t ret;

  /*
  ** erase the Fd receive set
  */
  FD_ZERO(&rucRdFdSet);

  for (i = 0; i <RUC_SOCKCTL_MAXPRIO ; i++)
  {
    ruc_sockctl_pnextCur = (ruc_obj_desc_t*)NULL;
    ruc_sockctl_prioIdxCur = RUC_SOCKCTL_MAXPRIO-1-i;

    while ((p = (ruc_sockObj_t*)
              ruc_objGetNext((ruc_obj_desc_t*)&ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO-1-i],
                             &ruc_sockctl_pnextCur))!=(ruc_sockObj_t*)NULL) 
    {
      ret = (*((p->callBack)->isRcvReadyFunc))(p->objRef,p->socketId);
      if(ret == TRUE)
      {
        /*
        ** The receiver is ready, assert the corresponding bit
        */
#if 0
      printf("prepareRcvBits :socketId %d, name: %s\n",p->socketId,&p->name[0]);
#endif

        FD_SET(p->socketId,&rucRdFdSet);
      }
    }
  }
}

static inline void ruc_sockCtl_prepareRcvAndXmitBits()
{

  int i;
  ruc_sockObj_t *p;
  uint32_t ret;



  for (i = 0; i <RUC_SOCKCTL_MAXPRIO ; i++)
  {
    ruc_sockctl_pnextCur = (ruc_obj_desc_t*)NULL;
    ruc_sockctl_prioIdxCur = RUC_SOCKCTL_MAXPRIO-1-i;

    while ((p = (ruc_sockObj_t*)
              ruc_objGetNext((ruc_obj_desc_t*)&ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO-1-i],
                             &ruc_sockctl_pnextCur))!=(ruc_sockObj_t*)NULL) 
    {
      if (ruc_applicative_poller != NULL)
      {
       (*ruc_applicative_poller)(0);
      }

      FD_CLR(p->socketId,&rucWrFdSet);
      FD_CLR(p->socketId,&rucRdFdSet);
      ret = (*((p->callBack)->isRcvReadyFunc))(p->objRef,p->socketId);
      if(ret == TRUE)
      {
        /*
        ** The receiver is ready, assert the corresponding bit
        */
#if 0
      printf("prepareRcvBits :socketId %d, name: %s\n",p->socketId,&p->name[0]);
#endif

        FD_SET(p->socketId,&rucRdFdSet);
      }
      ret = (*((p->callBack)->isXmitReadyFunc))(p->objRef,p->socketId);
      if(ret == TRUE)
      {
        /*
        ** The receiver is ready, assert the corresponding bit
        */
        FD_SET(p->socketId,&rucWrFdSet);
      }
    }
  }
}


/* #STARTDOC
**
**  #TITLE
void ruc_sockCtl_checkXmitBits()
**  #SYNOPSIS
**    That function check the xmit bit for each active
**    connection. If the bit is set the xmit function (provided
**    as call-back function)
**
**
**   IN:
**       none
**   OUT :
**       none
**
**
** ##ENDDOC
*/

void ruc_sockCtl_checkXmitBits()
{

  int i;
  ruc_sockObj_t *p;
  ruc_sockCallBack_t *pcallBack;
  int socketId;


  for (i = 0; i <RUC_SOCKCTL_MAXPRIO ; i++)
  {
    ruc_sockctl_pnextCur = (ruc_obj_desc_t*)NULL;
    ruc_sockctl_prioIdxCur = RUC_SOCKCTL_MAXPRIO-1-i;

    while ((p = (ruc_sockObj_t*)
              ruc_objGetNext((ruc_obj_desc_t*)&ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO-1-i],
                             &ruc_sockctl_pnextCur))!=(ruc_sockObj_t*)NULL) 
    {
      socketId = p->socketId;
      if(FD_ISSET(socketId, &rucWrFdSet))
      {
        /*
        ** the receive bit is set, call the related function
        ** and update rcv count for statistics purpose
        */
        p->xmitCount++;
        pcallBack = p->callBack;

        (*(pcallBack->xmitEvtFunc))(p->objRef,p->socketId);
	/*
	**  clear the corresponding bit
	*/
	FD_CLR(socketId,&rucWrFdSet);
      }
    }
  }
}


/* #STARTDOC
**
**  #TITLE
void ruc_sockCtl_prepareXmitBits()
**  #SYNOPSIS
**    That function calls the transmitter ready function of
**    each active connection. If the application replies
**    TRUE then the corresponding bit is set.
**
**   IN:
**       none
**   OUT :
**       none
**
**
** ##ENDDOC
*/

void ruc_sockCtl_prepareXmitBits()
{

  int i;
  ruc_sockObj_t *p;
  uint32_t ret;

  /*
  ** erase the Fd receive set
  */
  FD_ZERO(&rucWrFdSet);

  for (i = 0; i <RUC_SOCKCTL_MAXPRIO ; i++)
  {
    ruc_sockctl_pnextCur = (ruc_obj_desc_t*)NULL;
    ruc_sockctl_prioIdxCur = RUC_SOCKCTL_MAXPRIO-1-i;

    while ((p = (ruc_sockObj_t*)
              ruc_objGetNext((ruc_obj_desc_t*)&ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO-1-i],
                             &ruc_sockctl_pnextCur))!=(ruc_sockObj_t*)NULL) 
    {
      ret = (*((p->callBack)->isXmitReadyFunc))(p->objRef,p->socketId);
      if(ret == TRUE)
      {
        /*
        ** The receiver is ready, assert the corresponding bit
        */
        FD_SET(p->socketId,&rucWrFdSet);
      }
    }
  }
}

static inline void ruc_sockCtrl_roundRobbin()
{

  int i;
  ruc_sockObj_t *p;

  for (i = 0; i <RUC_SOCKCTL_MAXPRIO ; i++)
  {
    p = (ruc_sockObj_t *)
        ruc_objGetFirst((ruc_obj_desc_t*)&ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO-1-i]);
    if (p!= (ruc_sockObj_t*)NULL)
    {
       ruc_objRemove((ruc_obj_desc_t*)p);
       ruc_objInsertTail((ruc_obj_desc_t*)&ruc_sockCtl_tabPrio[RUC_SOCKCTL_MAXPRIO-1-i],
                          (ruc_obj_desc_t*)p);
    }
  }



}



#if 0
void ruc_sockCtrl_selectWait()
{
    int     nbrSelect;    /* nbr of events detected by select function */
    /*
    **  compute rucRdFdSet and rucWrFdSet
    */
    ruc_sockCtl_prepareRcvBits();
    ruc_sockCtl_prepareXmitBits();

    /*
    ** wait for event 
    */
    if((nbrSelect=select(FD_SETSIZE,&rucRdFdSet,&rucWrFdSet,NULL, NULL)) <= 0)
    {
      /*
      **  there is something rotten
      */
     return;
 
    }
    /*
    **  check for xmit bit set
    */
    ruc_sockCtl_checkXmitBits();

    /*
    **  check for receive bit set
    */
    ruc_sockCtl_checkRcvBits();
    /*
    **  insert the first element of each priority list at the
    **  tail of its priority list.
    */
    ruc_sockCtrl_roundRobbin();

    /*
    ** give a change to the NPS to process the
    ** message from its socket
    */
    sched_yield();

}

#endif

/**
* init of the system ticker
*/
void rozofs_init_ticker()
{
  struct timeval     timeDay;

  gettimeofday(&timeDay,(struct timezone *)0);  
  rozofs_ticker_microseconds = MICROLONG(timeDay);

}
uint64_t rozofs_ticker_microseconds = 0;  /**< ticker in microsecond ->see gettimeofday */
/**
*  Main loop
*/
void ruc_sockCtrl_selectWait()
{
    int     nbrSelect;    /* nbr of events detected by select function */
    struct timeval     timeDay;
    unsigned long long timeBefore, timeAfter;
//    uint32_t  	       timeOutLoopCount;  
    unsigned long long looptimeEnd,looptimeStart = 0;   
     timeBefore = 0;
     timeAfter  = 0;
//     timeOutLoopCount = 0;
	 int cpt_yield;

    /*
    ** erase the Fd receive set
    */
    FD_ZERO(&rucRdFdSet);
    FD_ZERO(&rucWrFdSet);   
    /*
    ** update time before call select
    */
    gettimeofday(&timeDay,(struct timezone *)0);  
    timeBefore = MICROLONG(timeDay);
    rozofs_ticker_microseconds = timeBefore;

	cpt_yield = 5;

    while (1)
    {
      /*
      **  compute rucRdFdSet and rucWrFdSet
      */
      ruc_sockCtl_prepareRcvAndXmitBits();
      //ruc_sockCtl_prepareXmitBits();
      //ruc_sockCtl_prepareRcvBits();

      
      gettimeofday(&timeDay,(struct timezone *)0);  
      looptimeEnd = MICROLONG(timeDay);  
      ruc_sockCtrl_looptime= (uint32_t)(looptimeEnd - looptimeStart); 
      if (ruc_sockCtrl_looptime > ruc_sockCtrl_looptimeMax)
      {
	  ruc_sockCtrl_looptimeMax = ruc_sockCtrl_looptime;
      }
	  
      /*
      ** give a change to the NPS to process the
      ** message from its socket
      */
      cpt_yield -=1;
	  if (cpt_yield == 0)
	  {
	    cpt_yield = 2;
		sched_yield();
	  }

      /*
      ** wait for event 
      */	  
      if((nbrSelect=select(FD_SETSIZE,&rucRdFdSet,&rucWrFdSet,NULL, NULL)) == 0)
      {
	/*
	** udpate time after select
	*/

	gettimeofday(&timeDay,(struct timezone *)0);  
	timeAfter = MICROLONG(timeDay); 
    rozofs_ticker_microseconds = timeAfter;
	looptimeStart  = timeAfter;
      }
      else
      {
//#warning Hello World -> to be removed just for fun
//    printf("Hello World!!\n");
      	gettimeofday(&timeDay,(struct timezone *)0);  
	looptimeStart = MICROLONG(timeDay); 
    rozofs_ticker_microseconds = looptimeStart;

	/*
	**  check for xmit bit set
	*/
	// ruc_sockCtl_checkXmitBits();

	/*
	**  check for receive bit set
	*/
	//ruc_sockCtl_checkRcvBits();
	ruc_sockCtl_checkRcvAndXmitBits(nbrSelect);
	/*
	**  insert the first element of each priority list at the
	**  tail of its priority list.
	*/
	ruc_sockCtrl_roundRobbin();
        gettimeofday(&timeDay,(struct timezone *)0);  
	timeAfter = MICROLONG(timeDay); 
    rozofs_ticker_microseconds = timeAfter;
      }


    }

}
