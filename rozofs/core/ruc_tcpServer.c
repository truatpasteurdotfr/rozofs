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

#include <stdio.h>
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
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <rozofs/common/types.h>
#include <rozofs/common/log.h>

#include "ruc_common.h"
#include "ruc_list.h"
#include "ruc_tcpServer.h"
#include "ruc_trace_api.h"



/*
** G L O B A L  D A T A
*/

/*
**  head of the free list of the Relay-c context
*/
ruc_tcp_server_t *ruc_tcp_server_freeList= (ruc_tcp_server_t*)NULL;
/*
** head of the active TCP  context
*/
ruc_tcp_server_t  *ruc_tcp_server_activeList= (ruc_tcp_server_t*)NULL;

/*
**   number of context
*/
uint16_t ruc_tcp_server_nbContext;
/*
**  Init flag
*/
int ruc_tcp_serverInitDone = FALSE;


/*
**  Call back function for socket controller
*/
ruc_sockCallBack_t ruc_tcp_server_callBack=
  {
     ruc_tcp_server_isRcvReady,
     ruc_tcp_server_acceptIn,
     ruc_tcp_server_isXmitReady,
     ruc_tcp_server_xmitEvent
  };




/*
**   F U N C T I O N S
*/



/*----------------------------------------------
   ruc_tcp_server_getObjRef
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
ruc_tcp_server_t *ruc_tcp_server_getObjRef(uint32_t tcpRef)
{


   uint32_t index;
   ruc_tcp_server_t *p;

#if 0
   uint32_t indexType;

   indexType = (tcpRef >> RUC_OBJ_SHIFT_OBJ_TYPE);
   if (indexType != RUC_TCP_SERVER_CTX_TYPE)
   {
     /*
     ** not a TCP CNX index type
     */
     return (ruc_tcp_server_t*)NULL;
   }
#endif
   /*
   **  Get the pointer to the context
   */
   index = tcpRef & RUC_OBJ_MASK_OBJ_IDX;
   p = (ruc_tcp_server_t*)ruc_objGetRefFromIdx((ruc_obj_desc_t*)ruc_tcp_server_freeList,
                                       index);
   return ((ruc_tcp_server_t*)p);
}



/*
**__________________________________________________________________________
*/
/**
    Create the socket and associate it with
    the TCP port provided as input parameter

   @param tcpPort   tcp well-known port
   @param ipAddr    source IP address (could be ANY)
   @param retry     Number of bind attempt to process in case of bind error
   
   @retval  -1 in case of error. The socket reference in case of sucess
*/
uint32_t ruc_tcp_server_createSocket_retry(uint16_t tcpPort,uint32_t ipAddr, int retry)
{
  int                 socketId;
  struct  sockaddr_in vSckAddr;
  int		      sock_opt;
  int                 ret;

  if((socketId = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    /*
    ** unable to create the socket
    */
    severe( "ruc_tcp_server_createSocket_retry socket error %u.%u.%u.%u:%u . Errno %d - %s",
      (ipAddr>>24)&0xFF,  (ipAddr>>16)&0xFF,(ipAddr>>8)&0xFF ,ipAddr&0xFF,
      tcpPort,
      errno, strerror(errno)
    );
    return((uint32_t)-1);
  }

  /* Tell the system to allow local addresses to be reused. */
  sock_opt = 1;
  if (setsockopt(socketId, SOL_SOCKET, SO_REUSEADDR, (void *)&sock_opt,sizeof (sock_opt)) == -1)
  {
     severe( "ruc_tcp_server_createSocket_retry setsockopt error %u.%u.%u.%u:%u . Errno %d - %s",
      (ipAddr>>24)&0xFF,  (ipAddr>>16)&0xFF,(ipAddr>>8)&0xFF ,ipAddr&0xFF,
      tcpPort,
      errno, strerror(errno)
     );
  }


  /*
  ** bind it to the well-known port
  */
  memset(&vSckAddr, 0, sizeof(struct sockaddr_in));
  vSckAddr.sin_family = AF_INET;
  vSckAddr.sin_port   = htons(tcpPort);
  vSckAddr.sin_addr.s_addr = htonl(ipAddr);
  
  ret = bind(socketId,(struct sockaddr *)&vSckAddr,sizeof(struct sockaddr_in));
  
  while (ret < 0) {
    
    if (retry <= 0) {
      /*
      **  error on socket binding
      */
      severe( "ruc_tcp_server_createSocket_retry BIND error %u.%u.%u.%u:%u . Errno %d - %s",
	(ipAddr>>24)&0xFF,  (ipAddr>>16)&0xFF,(ipAddr>>8)&0xFF ,ipAddr&0xFF,
	tcpPort,
	errno, strerror(errno)
      );
      close(socketId);
      return ((uint32_t)-1);
    }
    
    retry--;
    usleep(300000);
    ret = bind(socketId,(struct sockaddr *)&vSckAddr,sizeof(struct sockaddr_in));
  }

  return ((uint32_t) socketId);
}





// 64BITS uint32_t ruc_tcp_server_acceptIn(uint32 objRef,int socketId)
uint32_t ruc_tcp_server_acceptIn(void *p,int socketId)
{
   int fdWork;
   struct  sockaddr_in   sockAddr;
   int     sockAddrLen = sizeof(sockAddr);
   uint64_t objRef = (uint64_t)p;
   uint32_t ret;
   ruc_tcp_server_t *pObj;

   pObj =  ruc_tcp_server_getObjRef((uint32_t)objRef);
   if (pObj == (ruc_tcp_server_t*)NULL)
   {
      /*
      ** bad reference
      */
      return RUC_OK;
   }

   if((fdWork=accept(socketId,(struct sockaddr *)&sockAddr,(socklen_t *)&sockAddrLen)) == -1)
   {
     /*
     **  accept is rejected !
     */
     RUC_WARNING(errno);
     perror("error");
     return RUC_OK;
   }
   /*
   ** a new socket has been allocated. Call the applcaition that handles
   ** that connection (server) to figure out if the connection
   ** is acceptable or not
   */

   ret = (pObj->accept_CBK)(pObj->userRef,fdWork,(struct  sockaddr*)&sockAddr);
   if (ret == RUC_NOK)
   {
     /*
     ** the connection is rejected, so close the socket
     */
     close (fdWork);
     return RUC_OK;
   }
   return RUC_OK;
}



// 64BITS uint32_t ruc_tcp_server_isRcvReady(uint32 ref,int socketId)
uint32_t ruc_tcp_server_isRcvReady(void  *ref,int socketId)
{
  /*
  ** Always TRUE
  */
  return TRUE;
}

// 64BITS uint32_t ruc_tcp_server_isXmitReady(uint32 ref,int socketId)
uint32_t ruc_tcp_server_isXmitReady(void *ref,int socketId)
{

  /*
  ** Always FALSE
  */
  return FALSE;
}

// 64BITS uint32_t ruc_tcp_server_xmitEvent(uint32 ref,int socketId)
uint32_t ruc_tcp_server_xmitEvent(void *ref,int socketId)
{
  /*
  ** Always FALSE
  */
  return FALSE;

}








/*
**--------------------------------------
**    P U B L I C   F U N C T I O N S
**--------------------------------------
*/

/*----------------------------------------------
**
** uint32_t ruc_tcp_server_init(uint32 nbElements)
**----------------------------------------------
**  #SYNOPSIS
**   That service allocates resources in order
**   to handle nbElements of server connection.
**
**  That service must be called once only during the
**  application startup.
**
**   IN:
**       nbElements : number of server connection supported
**
**
**   OUT :
**      RUC_NOK : error during the initialization
**      RUC_OK : module up and ready.
**
**----------------------------------------------
*/

uint32_t ruc_tcp_server_init(uint32_t nbElements)
{
  uint32_t         ret = RUC_OK;
  uint32_t         curRef;
  ruc_obj_desc_t *pnext ;
  ruc_tcp_server_t    *p;

  if (ruc_tcp_serverInitDone != FALSE)
  {
    return RUC_NOK;
  }
  while (1)
  {
    /*
    ** allocate the free connection distributor
    */
    ruc_tcp_server_freeList = (ruc_tcp_server_t*)ruc_listCreate(nbElements,sizeof(ruc_tcp_server_t));
    if (ruc_tcp_server_freeList == (ruc_tcp_server_t*)NULL)
    {
      /*
      ** error on distributor creation
      */
      severe( "ruc_listCreate(%d,%d)", (int)nbElements,(int)sizeof(ruc_tcp_server_t) )
      ret = RUC_NOK;
      break;
    }
    /*
    ** init of the active list
    */
    ruc_tcp_server_activeList = (ruc_tcp_server_t*)malloc(sizeof(ruc_tcp_server_t));
    if (ruc_tcp_server_activeList == (ruc_tcp_server_t*)NULL)
    {
      /*
      ** out of memory
      */
      severe( "ruc_tcp_server_activeList = malloc(%d)", (int)sizeof(ruc_tcp_server_t) )
      ret = RUC_NOK;
      break;
    }
    ruc_listHdrInit((ruc_obj_desc_t*)ruc_tcp_server_activeList);

    /*
    ** initialize each entry of the distributor
    */
    curRef = 0;
    pnext = (ruc_obj_desc_t*)NULL;
    while ((p = (ruc_tcp_server_t*)ruc_objGetNext((ruc_obj_desc_t*)ruc_tcp_server_freeList,
                                             &pnext))
               !=(ruc_tcp_server_t*)NULL)
    {
      p->ref = curRef | (RUC_TCP_SERVER_CTX_TYPE<<RUC_OBJ_SHIFT_OBJ_TYPE);
      p->cnxName[0]= 0;
      p->accept_CBK = (ruc_tcp_server_recvCBK_t)NULL;
      p->socketId =(uint32_t)-1;
      p->connectId= NULL;

      curRef++;
    }

    /*
    **  all is done
    */
    break;
  }
  /*
  ** check if there was an error during the initialization
  */
  if (ret != RUC_OK)
  {
    /*
    ** an error has been encountered: purge everything
    */
    return RUC_NOK;
  }
  /*
  ** everything is OK: marked it
  */
  ruc_tcp_serverInitDone = TRUE;
  return ret;
}


/*--------------------------------------------------------------------
**
** uint32_t ruc_tcp_server_connect(ruc_tcp_server_connect_t *pconnect)
**--------------------------------------------------------------------
**  #SYNOPSIS
**   That function performs the creation of a server port. The caller
**  should provide the port and the IPaddr. The connection can be
**  performed for any input interface (ANY_INADDR).
**
** On success, the reference of the server connection is returned.
**
**  The caller provides a callback for accept(). When a new connection
**  is received, then the TCP server connection handler, calls that
**  callback with the following arguments:
**
**     - user reference given in the connection block;
**     - new socket identifier
**     - address and port of the initiator
**
**  The application may accept or reject the new connection. If the
** connection is accepted, then RUC_OK must returned, otherwise
** RUC_NOK is returned.
**
**  note: there is no need for socket deletion in case of reject, this
**  is doen by the TCP server handler.
**
**  The following action are performed:
**   - creation of the socket
**   - do the listen
**   - do the connection with the socket controller
**
**  the caller should provide the following information:
**    - name of the connection (16 max)
**    - callback for accept service
**    - IP address (host format)
**    - TCP port host format)
**    - userRef (for accept callback)
**
**   IN:
**      pconnect: pointer to the connection info block
**
**
**   OUT :
**      -1: error
**      !=-1: reference of the connection block
**
**----------------------------------------------
*/

uint32_t ruc_tcp_server_connect(ruc_tcp_server_connect_t *pconnect)
{
 ruc_tcp_server_t *pObj;

  /*
  ** get the first free Relci context
  */
  pObj = (ruc_tcp_server_t*)ruc_objGetFirst((ruc_obj_desc_t*)ruc_tcp_server_freeList);
  if (pObj == (ruc_tcp_server_t*)NULL)
  {
    /*
    ** out of free context
    */
    severe( "ruc_tcp_server_connect: Out of free context" )
    return (uint32_t)-1;
  }

   /*
   ** record the user parameters
   */
   pObj->tcpPort = pconnect->tcpPort;
   pObj->ipAddr = pconnect->ipAddr;
   pObj->priority = pconnect->priority;
   pObj->accept_CBK = pconnect->accept_CBK;
   pObj->userRef    = pconnect->userRef;
   memcpy(&pObj->cnxName[0],&pconnect->cnxName[0],RUC_TCP_SERVER_NAME);
   pObj->cnxName[RUC_TCP_SERVER_NAME-1] = 0;
   /*
   ** create the TCP socket
   */
   pObj->socketId =  ruc_tcp_server_createSocket_retry(pObj->tcpPort,pObj->ipAddr,9);
   if (pObj->socketId == -1)
   {
     /*
     ** unable to create the socket -> error
     */
     severe( " ruc_tcp_server_connect: unable to create the server socket %u.%u.%u.%u:%d",
       pObj->ipAddr>>24&0xFF, pObj->ipAddr>>16&0xFF, pObj->ipAddr>>8&0xFF, pObj->ipAddr&0xFF,
       pObj->tcpPort )
     return (uint32_t) -1;
   }

   if((listen(pObj->socketId,RUC_TCP_SERVER_BACKLOG))==-1)
   {
     severe( " ruc_tcp_server_connect: listen fails for %s ,",pObj->cnxName )
     return (uint32_t)-1;
   }
   /*
   **  perform the connection with the socket controller
   */
   uint64_t ObjRef = (uint64_t) pObj->ref;  //64BITS
   pObj->connectId = ruc_sockctl_connect (pObj->socketId,
                                          (char*)pObj->cnxName,
                                          pObj->priority,
                                          //64BITS pObj->ref,
                                          (void*) ObjRef,
                                          &ruc_tcp_server_callBack);
   if (pObj->connectId ==  NULL)
   {
     /*
     **  fatal error while connecting with the socket controller
     */
     severe( " ruc_tcp_server_connect: unable to connect with the socket controller (%s) ,",pObj->cnxName )
     return (uint32_t)-1;
   }
   /*
   ** all is fine:
   ** remove it from the free list and insert it in the active list
   */
   ruc_objRemove((ruc_obj_desc_t*)pObj);
   ruc_objInsertTail((ruc_obj_desc_t*)ruc_tcp_server_activeList,(ruc_obj_desc_t*)pObj);

   return pObj->ref;
}
/*--------------------------------------------------------------------
**
** uint32_t ruc_tcp_server_disconnect(uint32 cnxRef)
**--------------------------------------------------------------------
**  #SYNOPSIS
**   That function performs the disconnection of a TCP server
**
**   IN:
**      cnxRef: reference returned on the ruc_tcp_server_connect()
**
**   OUT : RUC_OK/RUC_NOK
**
**----------------------------------------------
*/

uint32_t ruc_tcp_server_disconnect (uint32_t cnxRef)
{
  ruc_tcp_server_t *pObj;

  /*
  ** get the first free Relci context
  */
  pObj = ruc_tcp_server_getObjRef(cnxRef);
  if (pObj == (ruc_tcp_server_t*)NULL)
   {
    /*
    ** out of free context
    */
    severe( "ruc_tcp_server_disconnect: Bad cnxRef" )
    return RUC_NOK;
  }

  /*
  **  perform the disconnection with the socket controller
  */
  ruc_sockctl_disconnect(pObj->connectId);

  /*
  ** Free the socket
  */
  close(pObj->socketId);

  /*
  ** all is fine:
  ** remove it from the free list and insert it in the active list
  */
  ruc_objRemove((ruc_obj_desc_t*)pObj);
  ruc_objInsertTail((ruc_obj_desc_t*)ruc_tcp_server_freeList,(ruc_obj_desc_t*)pObj);

  return RUC_OK;
}
