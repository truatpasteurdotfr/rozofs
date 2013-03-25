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
#define RUC_OBSERVER_C
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdlib.h>

#include "ruc_observer_api.h"
#include "ppu_trace.h"
/*
**-------------------------------------------------------------
**   S T U C T U R E
**
**  +--------------+              +--------------+
**  |              | SERVER CTX   |              | SERVER CTX
**  | . link <--------------------->. link <------------
**  |              |              |              |
**  |              |              |              |
**  |              |              |              |
**  | . pEvent --------+          | . pEvent  --------> EVENT ARRAY
**  |              |   |          |              |
**  +--------------+   |          +--------------+
**                     |
**                     +-> +------------+
**                         |0           | EVENT ARRAY
**                         |1           | allocated on
**                         |2           | ruc_observer_declareServer
**                         |.           |
**                         |.           |
**                         |.           |      CLIENT CTX
**                         |N     head <----->+---------+ +---------+ +---------+
**                         |.           |     |         | |         | |         |
**                         |.           |     | . link <--> . link <--> . link <-->
**                         +------------+     |         | |         | |         |
**                                            +---------+ +---------+ +---------+

**-------------------------------------------------------------
*/

/*
**  Maxcallback number of callback entries
*/
#define RUC_OBSERVER_NAME_MAX      32

typedef struct _ruc_observer_client_t {
  ruc_obj_desc_t        link;
  uint32_t                ref;
  uint32_t                srvRef;
  uint32_t                priority;
  uint32_t                event;
//64BITS  uint32_t                objRef;
  void                 *objRef;
  ruc_observer_cbk      callBack;
  char                  name[RUC_OBSERVER_NAME_MAX];
} RUC_OBSERVER_CLIENT_T;


typedef struct _ruc_observer_event_t {
  ruc_obj_desc_t        head; /* Head of client list */
  ruc_obj_desc_t      * pnextCur;
  uint32_t                event;
} RUC_OBSERVER_EVENT_T;

typedef struct _ruc_observer_server_t {
  ruc_obj_desc_t                 link;
  uint32_t                         ref;
  char                           name[RUC_OBSERVER_NAME_MAX];
  uint32_t                         nbEvent;
  RUC_OBSERVER_EVENT_T         * pEventTbl;
} RUC_OBSERVER_SERVER_T;


/*
** head of the free context list
*/

uint32_t                      ruc_observer_initDone = FALSE;

/* Pool of call back descriptors. Each of them descibes a client */
RUC_OBSERVER_CLIENT_T     * ruc_observer_client_freeListHead = (RUC_OBSERVER_CLIENT_T*)NULL;
ruc_obj_desc_t            * ruc_observer_pnextCur = (ruc_obj_desc_t*) NULL;


/* Pool of server context descriptor. On each of them is chained the client call abck descriptors */
RUC_OBSERVER_SERVER_T     ruc_observer_server_activeList;
RUC_OBSERVER_SERVER_T   * ruc_observer_server_freeListHead = (RUC_OBSERVER_SERVER_T*)NULL;
ruc_obj_desc_t          * ruc_observer_server_pnextCur = (ruc_obj_desc_t*) NULL;


uint32_t             ruc_observer_max_server=0;
uint32_t             ruc_observer_max_client=0;
char                ruc_observer_buffer[4096];

/*
**-------------------------------------------------------------
**   G L O B A L   D A T A
**
**-------------------------------------------------------------
*/

/*
**-------------------------------------------------------------
**   S E R V I C E S
**
**-------------------------------------------------------------
*/

/*-----------------------------------------------
**   ruc_observer_getServer
**-----------------------------------------------
** based on the object index, that function
** returns the pointer to the object context.
**
** That function may fails if the index is
** not a MS context index type.
**
**    IN  : MS index
**    OUT : NULL if error
**-----------------------------------------------
*/
RUC_OBSERVER_SERVER_T *ruc_observer_getServer(uint32_t index) {
  RUC_OBSERVER_SERVER_T *p;

  /*
  **  Get the pointer to the context
  */
  if ( index >= ruc_observer_max_server){
    /*
    ** the MS index is out of range
    */
    ERRFAT "ruc_observer_getServer(%d): index is out of range, index max is %d",index, ruc_observer_max_server ENDERRFAT;
    return (RUC_OBSERVER_SERVER_T*)NULL;
  }
  p = (RUC_OBSERVER_SERVER_T*)ruc_objGetRefFromIdx(&ruc_observer_server_freeListHead->link,index);
  return ((RUC_OBSERVER_SERVER_T*)p);
}
/*-----------------------------------------------
**   ruc_observer_getClient
**-----------------------------------------------
** based on the object index, that function
** returns the pointer to the object context.
**
** That function may fails if the index is
** not a MS context index type.
**
**    IN  : MS index
**    OUT : NULL if error
**-----------------------------------------------
*/
RUC_OBSERVER_CLIENT_T *ruc_observer_getClient(uint32_t index) {
  RUC_OBSERVER_CLIENT_T *p;

  /*
  **  Get the pointer to the context
  */
  if ( index >= ruc_observer_max_client) {
    /*
    ** the MS index is out of range
    */
    ERRFAT "ruc_observer_getClient(%d) index is out of range, index max is %d",index, ruc_observer_max_client ENDERRFAT;
    return (RUC_OBSERVER_CLIENT_T*)NULL;
  }
  p = (RUC_OBSERVER_CLIENT_T*)ruc_objGetRefFromIdx(&ruc_observer_client_freeListHead->link, index);
  return ((RUC_OBSERVER_CLIENT_T*)p);
}
/*-----------------------------------------------
**   ruc_observer_updateEventCur
**-----------------------------------------------
**
**    IN  : MS index
**    OUT : NULL if error
**-----------------------------------------------
*/
void ruc_observer_updateEventCur(RUC_OBSERVER_EVENT_T  * pEvent,
				 RUC_OBSERVER_CLIENT_T * pClt) {
  ruc_obj_desc_t *pfake;

  if (pEvent->pnextCur != (ruc_obj_desc_t*)pClt) {
    /*
    ** nothing to do
    */
    return;
  }
  /*
  ** ruc_observer_client_pnextCur needs to be updated
  */
  pfake = ruc_objGetNext(&pEvent->head, &pEvent->pnextCur);
}
/*----------------------------------------------
**   init of the observer service
**----------------------------------------------
**  IN :
**     . maxServer = number server context
**     . maxClient = number of client context
**  OUT : RUC_OK or RUC_NOK
**-----------------------------------------------
*/
uint32_t ruc_observer_init(uint32_t maxClient, uint32_t maxServer) {
  int                         idx;
  ruc_obj_desc_t            * pnext=(ruc_obj_desc_t*)NULL;
  RUC_OBSERVER_SERVER_T     * pSrv;
  RUC_OBSERVER_CLIENT_T     * pClt;

  if (ruc_observer_initDone == TRUE) {
    return RUC_OK;
  }

  ruc_observer_max_server = maxServer;
  ruc_observer_max_client = maxClient;
  /*
  ** create the server distributor
  */
  ruc_observer_server_freeListHead =
    (RUC_OBSERVER_SERVER_T *)ruc_listCreate(maxServer, sizeof(RUC_OBSERVER_SERVER_T));
  if (ruc_observer_server_freeListHead == (RUC_OBSERVER_SERVER_T*)NULL) {
    /*
    ** out of memory
    */
    ERRFAT "runc_observer_init: out of memory" ENDERRFAT;
    return RUC_NOK;
  }
  /*
  **  initialize each element of the free list
  */
  idx = 0;
  pnext = (ruc_obj_desc_t*)NULL;
  while ((pSrv = (RUC_OBSERVER_SERVER_T*)
	  ruc_objGetNext(&ruc_observer_server_freeListHead->link, &pnext))
	 !=(RUC_OBSERVER_SERVER_T*)NULL)  {
    pSrv->nbEvent   = 0;
    pSrv->ref       = idx;
    pSrv->pEventTbl = NULL;
    strcpy(pSrv->name,"FREE");
    idx +=1;
  }
  ruc_listHdrInit(&ruc_observer_server_activeList.link);



  /*
  ** create the client distributor
  */
  ruc_observer_client_freeListHead =
    (RUC_OBSERVER_CLIENT_T *)ruc_listCreate(maxClient, sizeof(RUC_OBSERVER_CLIENT_T));
  if (ruc_observer_client_freeListHead == (RUC_OBSERVER_CLIENT_T*)NULL) {
    /*
    ** out of memory
    */
    ERRFAT "runc_observer_init: out of memory" ENDERRFAT;
    return RUC_NOK;
  }
  /*
  **  initialize each element of the free list
  */
  idx = 0;
  pnext = (ruc_obj_desc_t*)NULL;
  while ((pClt = (RUC_OBSERVER_CLIENT_T*)
              ruc_objGetNext((ruc_obj_desc_t*)ruc_observer_client_freeListHead, &pnext))
	 !=(RUC_OBSERVER_CLIENT_T*)NULL)  {
    pClt->priority  = -1;
    pClt->ref       = idx;
    pClt->objRef    = NULL;
    pClt->event     = -1;
    pClt->callBack  = (ruc_observer_cbk)NULL;
    strcpy(pClt->name,"FREE");
    idx +=1;
  }

  ruc_observer_initDone = TRUE;
  return RUC_OK;

}
/*----------------------------------------------
**   Declare a server
**----------------------------------------------
**  IN :
**     . name : name of the server
**     . nbEvent : number of events generated
**                 by the server
**
**  OUT : Server reference or -1
**-----------------------------------------------
*/
uint32_t ruc_observer_declareServer(char * name, uint32_t nbEvent) {
  RUC_OBSERVER_SERVER_T     * pSrv;
  uint32_t                      idx;

  if (ruc_observer_initDone == FALSE) {
    ERRFAT "OBSERVER not initialised" ENDERRFAT;
    return -1;
  }

  /*
  ** get the first element from the free list
  */
  pSrv = (RUC_OBSERVER_SERVER_T*)ruc_objGetFirst((ruc_obj_desc_t*) ruc_observer_server_freeListHead);
  if (pSrv == (RUC_OBSERVER_SERVER_T* )NULL) {
    ERRFAT "Out of server context" ENDERRFAT;
    return -1;
  }
  /*
  **  remove the context from the free list
  */
  ruc_objRemove(&pSrv->link);
  /*
  **  store the callback pointer,the user Reference and priority
  */

  strncpy(pSrv->name,name,RUC_OBSERVER_NAME_MAX);
  pSrv->name[RUC_OBSERVER_NAME_MAX-1] = 0;
  pSrv->nbEvent = nbEvent;

  /*
  ** Allocate an event table
   */
  pSrv->pEventTbl = (RUC_OBSERVER_EVENT_T *)malloc(nbEvent*sizeof(RUC_OBSERVER_EVENT_T));
  if (pSrv->pEventTbl == (RUC_OBSERVER_EVENT_T*)NULL) {
    /*
    ** out of memory
    */
    ERRFAT "Out of memory" ENDERRFAT;
    return -1;
  }
  /*
  **  initialize each event
  */
  for (idx=0; idx < nbEvent; idx++) {
    pSrv->pEventTbl[idx].event = idx;
    pSrv->pEventTbl[idx].pnextCur = NULL;
    ruc_listHdrInit(&pSrv->pEventTbl[idx].head);
  }

  /*
  **  insert in the server active list
  */
  ruc_objInsertTail(&ruc_observer_server_activeList.link, &pSrv->link);

  return (pSrv->ref);
}
/*----------------------------------------------
**   Create a client context
**----------------------------------------------
**  IN :
**     . servRef : reference of the server to bind on
**     . clientName : name of the client callback function
**     . event : event generated by the server to bind on
**     . priority : priority within the client list
**     . userRef : 1rst call back parameter
**     . cbk : callback function address
**  OUT : the client reference or -1
**-----------------------------------------------
*/
uint32_t ruc_observer_createClient(uint32_t servRef,
				 char           * clientName,
				 uint32_t           event,
				 uint32_t           priority,
//64BITS				 uint32_t           userRef,
				 void           *userRef,
				 ruc_observer_cbk cbk) {
  RUC_OBSERVER_CLIENT_T   * pClt;
  RUC_OBSERVER_SERVER_T   * pSrv;
  ruc_obj_desc_t          * phead, * pnext;

  if (ruc_observer_initDone == FALSE) {
    ERRFAT "OBSERVER not initialised" ENDERRFAT;
    return -1;
  }

  /*
  ** Find the server context
  */
  pSrv = ruc_observer_getServer(servRef);
  if (pSrv == NULL) {
    ERRFAT "Unknown server reference %u", servRef ENDERRFAT;
    return -1;
  }

  /*
  ** check that the event is not out of range
  */
  if (event >= pSrv->nbEvent) {
    ERRLOG "Event is out of range: %d. Max is %u for %s",event, pSrv->nbEvent, pSrv->name ENDERRLOG
    return -1;
  }


  /*
  ** get the first element from the free list
  */
  pClt = (RUC_OBSERVER_CLIENT_T*)ruc_objGetFirst((ruc_obj_desc_t*) ruc_observer_client_freeListHead);
  if (pClt == (RUC_OBSERVER_CLIENT_T* )NULL) {
    ERRLOG "No more free client" ENDERRLOG
    return  -1;
  }
  /*
  **  remove the context from the free list
  */
  ruc_objRemove(&pClt->link);
  /*
  **  store the callback pointer,the user Reference and priority
  */

  pClt->srvRef   = pSrv->ref;
  pClt->objRef   = userRef;
  pClt->callBack = cbk;
  pClt->priority = priority;
  pClt->event    = event;
  strncpy(pClt->name,clientName,RUC_OBSERVER_NAME_MAX);
  pClt->name[RUC_OBSERVER_NAME_MAX-1] = 0;

  /*
  **  insert in the associated event list (ordered list)
  */

  phead = &((pSrv->pEventTbl[event]).head);
  pnext = phead;
  while (((pnext = pnext->ps) != phead) &&
	 (((RUC_OBSERVER_CLIENT_T*)pnext)->priority > (pClt->priority)));
  pClt->link.ps       = (ruc_obj_desc_t*) pnext;
  pnext->pp->ps       = (ruc_obj_desc_t*) pClt;
  pClt->link.pp       = pnext->pp;
  pnext->pp           = (ruc_obj_desc_t*) pClt;

  return (pClt->ref);
}


/*----------------------------------------------
**   remove a client context
**----------------------------------------------
**  IN :
**     . cltRef : the client context reference
**  OUT : RUC_OK or RUC_NOK
**-----------------------------------------------
*/
uint32_t ruc_observer_removeClient(uint32_t cltRef) {
  RUC_OBSERVER_CLIENT_T * pClt;
  RUC_OBSERVER_SERVER_T * pSrv;

  if (ruc_observer_initDone == FALSE) {
    ERRFAT "OBSERVER not initialised" ENDERRFAT;
    return RUC_NOK;
  }

  pClt = ruc_observer_getClient(cltRef);
  if (pClt == NULL) {
    return RUC_NOK;
  }

  /*
  ** Find the server context
  */
  pSrv = ruc_observer_getServer(pClt->srvRef);
  if (pSrv == NULL) {
    ERRLOG "Unknown server reference %u for client %s", pClt->srvRef, pClt->name  ENDERRLOG;
    return RUC_NOK;
  }

  /*
  ** Check the event number is valid
  */
  if (pClt->event >= pSrv->nbEvent) {
    ERRLOG "The event value is out of range %d for client %s", pClt->event, pClt->name ENDERRLOG;
    return RUC_OK;
  }

  ruc_observer_updateEventCur(&(pSrv->pEventTbl[pClt->event]),pClt);

  /*
  **  Unchain it from the event
  */
  ruc_objRemove(&pClt->link);
  /*
  **  set it free
  */
  pClt->priority  = -1;
  pClt->objRef    = NULL;
  pClt->event     = -1;
  pClt->callBack  = (ruc_observer_cbk)NULL;
  strcpy(pClt->name,"FREE");

  /*
  **  insert in the free list
  */
  ruc_objInsertTail(&ruc_observer_client_freeListHead->link, &pClt->link);

  return RUC_OK;
}


/*----------------------------------------------
**   Process a call back when the server generates
**   an event
**----------------------------------------------
**  IN :
**     . srvRef : the server reference
**     . event : the event generated
**     . srvParam : a parameter determined by the server
**  OUT : none
**-----------------------------------------------
*/
//64BITS void  ruc_observer_serverEvent(uint32_t srvRef, uint32 event, uint32 srvParam)
void  ruc_observer_serverEvent(uint32_t srvRef, uint32_t event, void *srvParam)
{
  RUC_OBSERVER_CLIENT_T * pClt;
  RUC_OBSERVER_SERVER_T * pSrv;
  ruc_obj_desc_t        * pnext, * phead;

  if (ruc_observer_initDone == FALSE) {
    ERRFAT "OBSERVER not initialised" ENDERRFAT;
    return;
  }

  /*
  ** Find the server context
  */
  pSrv = ruc_observer_getServer(srvRef);
  if (pSrv == NULL) {
    ERRLOG "Unknown server reference %u", srvRef ENDERRLOG;
    return;
  }

  /*
  ** Check the event number is valid
  */
  if (event >= pSrv->nbEvent) {
    ERRLOG "The event value is out of range %d. max is %u for server %s", event, pSrv->nbEvent, pSrv->name ENDERRLOG;
    return;
  }

  pnext = (ruc_obj_desc_t*)NULL;
  phead = &((pSrv->pEventTbl[event]).head);
  while ((pClt = (RUC_OBSERVER_CLIENT_T *)
	  ruc_objGetNext(phead,&pnext))!=(RUC_OBSERVER_CLIENT_T*)NULL)
  {
   /*
   ** call the user function
   */
  (*(pClt->callBack))(pClt->objRef,srvParam);
 }
}


/*----------------------------------------------
**   print the configuration of a server
**----------------------------------------------
**  IN :
**     . srvRef : the server reference
**     . event : the event generated
**     . srvParam : a parameter determined by the server
**  OUT : none
**-----------------------------------------------
*/
char *ruc_observer_print_configuration_for_server(  RUC_OBSERVER_SERVER_T * pSrv,char *buf)
{
  ruc_obj_desc_t        * pnext, * phead;
  RUC_OBSERVER_CLIENT_T * pClt;
  int i;

  buf +=sprintf(buf,"name: %s\n",pSrv->name);
  for (i = 0; i < pSrv->nbEvent; i++)
  {
    buf +=sprintf(buf,"  evt[%d]= %d\n",i,pSrv->pEventTbl[i].event);
    buf +=sprintf(buf,"    Client List\n");
    pnext = (ruc_obj_desc_t*)NULL;
    phead = &((pSrv->pEventTbl[i]).head);
    while ((pClt = (RUC_OBSERVER_CLIENT_T *)
	    ruc_objGetNext(phead,&pnext))!=(RUC_OBSERVER_CLIENT_T*)NULL)
    {
       buf +=sprintf(buf,"    %s (%p(%p))\n",pClt->name,pClt->callBack,pClt->objRef);
    }
  }
  buf+=sprintf(buf,"\n");
  return buf;
}


/*----------------------------------------------
**   print the configuration for all servers
**----------------------------------------------
**  IN :

**  OUT : none
**-----------------------------------------------
*/
char *ruc_observer_print_all_conf(char *buf)
{

  ruc_obj_desc_t            * pnext=(ruc_obj_desc_t*)NULL;
  RUC_OBSERVER_SERVER_T     * pSrv;

  if (ruc_observer_initDone != TRUE) {
    buf+=sprintf(buf,"Service is not yet configured !!\n");
    return buf;
  }
  pnext = (ruc_obj_desc_t*)NULL;
  while ((pSrv = (RUC_OBSERVER_SERVER_T*)
	  ruc_objGetNext(&ruc_observer_server_activeList.link, &pnext))
	 !=(RUC_OBSERVER_SERVER_T*)NULL)  {
     buf =ruc_observer_print_configuration_for_server(pSrv,buf);
  }
  return buf;
}


void  ruc_observer_debug_conf()
{
  char *buf = ruc_observer_buffer;

  buf = ruc_observer_print_all_conf(buf);
  printf("%s",ruc_observer_buffer);


}
