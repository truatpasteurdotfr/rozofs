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
#include <rozofs/core/ruc_trace_api.h>
#include <rozofs/core/ruc_common.h>
#include "rozofs_export_gateway_conf_non_blocking.h"
#include <rozofs/core/ruc_sockCtl_api.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/eclient.h>
#include <rozofs/core/expgw_common.h>


uint32_t  rozofs_exp_ready4configuration = 0;
uint32_t  rozofs_exp_getIntSockIdxFromSocketId(rozofs_exp_sup_conf_t *p,int socketId);
/*
**   G L O B A L    D A T A
*/

/*
**  Call back function for socket controller
*/
ruc_sockCallBack_t rozofs_exp_callBack_InternalSock=
  {
     rozofs_exp_rcvReadyInternalSock,
     rozofs_exp_rcvMsgInternalSock,
     rozofs_exp_xmitReadyInternalSock,
     rozofs_exp_xmitEvtInternalSock
  };

rozofs_exp_sup_conf_t   rozofs_exp_lbg_ctx;


 uint32_t rozofs_exp_sup_conf_trace = TRUE;



/*----------------------------------------------
**  rozofs_exp_getObjRef
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

rozofs_exp_sup_conf_t *rozofs_exp_getObjRef()
{
  return (rozofs_exp_sup_conf_t *)&rozofs_exp_lbg_ctx;
}

/*
**_________________________________________________________________________
*/
/**
* send the configuration of a load balancing group to the non blocking side
  It is assumed that the load balancing group has been previously created

  The message is sent over the socket AF_UNIX pair.
  
  @param mstorage : pointer to the storage node data structure
  

  @retval 0 success
  @retval -1 error
*/
int rozofs_exp_send_lbg_port_configuration(uint32_t opcode, void *mstorage )
{
  int nBytes;
  uint32_t  response;
  rozofs_exp_msg_t msg;
  rozofs_exp_sup_conf_t *p = &rozofs_exp_lbg_ctx;

  msg.opcode = opcode;
  msg.param = mstorage;

  nBytes = send(p->internalSocket[RUC_SOC_SEND],
                (const char *)&msg,
                sizeof(rozofs_exp_msg_t),
                0);
  if (nBytes != sizeof(rozofs_exp_msg_t))
  {
    /*
    **  message not sent
    */
    severe("rozofs_exp_send_lbg_port_configuration : %s",strerror(errno));
    return -1;
  }
  /*
  ** OK now wait for the response
  */
    nBytes = recv(p->internalSocket[RUC_SOC_SEND],
                   (char *)&response,
                   sizeof(uint32_t),
                   0);
  if (nBytes != sizeof(uint32_t))
  {
    /*
    **  something wrong : (BUG)
    */
    severe("rozofs_exp_send_lbg_port_configuration : %s",strerror(errno));
    return -1;
  }
  return (int)response;
}

/*
**_________________________________________________________________________
*/
/**
*  send an export gateway configuration message udpate/creation
   for the main thread

  @param expgw_conf_p: pointer to the export gateway configuration (decoded)
  
  @retval 0 on success
  @retval -1 on error
*/
int rozofs_exp_reload_export_gateway_conf(ep_gateway_configuration_t *expgw_conf_p )
{
  int nBytes;
  rozofs_exp_msg_t msg;
  uint32_t  response;
  rozofs_exp_sup_conf_t *p = &rozofs_exp_lbg_ctx;

  msg.opcode = ROZOFS_EXPORT_GW_CONF;
  msg.param = expgw_conf_p;

  nBytes = send(p->internalSocket[RUC_SOC_SEND],
                (const char *)&msg,
                sizeof(rozofs_exp_msg_t),
                0);
  if (nBytes != sizeof(rozofs_exp_msg_t))
  {
    /*
    **  message not sent
    */
    severe("rozofs_exp_send_lbg_create : %s",strerror(errno));
    return -1;

  }
  /*
  ** OK now wait for the response
  */
    nBytes = recv(p->internalSocket[RUC_SOC_SEND],
                   (char *)&response,
                   sizeof(uint32_t),
                   0);
  if (nBytes != sizeof(uint32_t))
  {
    /*
    **  something wrong : (BUG)
    */
    severe("rozofs_exp_send_lbg_create : %s",strerror(errno));
    return -1;
  }
  return (int)response;
} 


/*
**_________________________________________________________________________
*/
/**
*  Process the configuration of the export gateway received from the 
   main process:
   Upon the reception of the export gateway from the exportd , the configuration
   supervision thread sends the decoded received configuration to the non-blocking
   part of the rozofsmount.
   That message is received on an AF_UNIX socket (socket pair).
   Upon processing the configuartion, the non blocking side sends back a status to
   the supervision thread.
   
   
  @param p: pointer to the socket pair context
  @param msg: pointer to the received message
  
  @retval 0 on success
  @retval -1 on error
*/
int rozofs_exp_process_export_gateway_conf_nb(rozofs_exp_sup_conf_t *p,rozofs_exp_msg_t *msg)
{
    uint32_t response = 0;
    int status;
    uint32_t *eid_p;
    int rank;
    int i;
    int nBytes;
    
    ep_gateway_configuration_t  *arg = msg->param;
    
    
    /*
    ** move to dirty the old configuration
    */
    expgw_set_exportd_table_dirty(arg->hdr.export_id);
    /*
    ** Insert the configuration
    */
    eid_p = arg->eid.eid_val;
    status = 0;
    while(1)
    {
      for (i = 0; i < arg->eid.eid_len;i++)
      {
        status = expgw_export_add_eid(arg->hdr.export_id,   // exportd id
                                   eid_p[i],                // eid
                                   arg->exportd_host,       // hostname of the Master exportd
                                   0,                       // port
                                   arg->hdr.nb_gateways,    // nb Gateway
                                   arg->hdr.nb_gateways    // gateway rank =  nb Gateway for Rozofsmount
                                   );    
        if (status < 0) 
        {
          severe("fail to add eid %d",i);
          response = (uint32_t) -1;
          break;
        }    
      }
      if (status < 0) break;
      /*
      ** insert the hosts
      */
      for (rank = 0; rank < arg->hdr.nb_gateways; rank++)
      {
        status = expgw_add_export_gateway(arg->hdr.export_id, 
                                         (char*)arg->gateway_host.gateway_host_val[rank].host,
                                         60000+EXPGW_PORT_ROZOFSMOUNT_IDX,
                                         rank); 
        if (status < 0) 
        {
          severe("fail to add host %s",(char*)arg->gateway_host.gateway_host_val[rank].host);
          response = (uint32_t) -1;
          break;
        }           
      }
      /*
      ** OK now clear the remain dirty entries
      */
      expgw_clean_up_exportd_table_dirty(arg->hdr.export_id);

      break;
    }
    /*
    ** send back the response to the supervision thread
    */
    nBytes = send(p->internalSocket[RUC_SOC_RECV],
                  (const char *)&response,
                  sizeof(response),
                  0);
    if (nBytes != sizeof(response))
    {
      /*
      **  message not sent
      */
      fatal("error on sending response");

    }   
 
    return (int) response;
} 
/*----------------------------------------------
**  rozofs_exp_getIntSockIdxFromSocketId
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

uint32_t  rozofs_exp_getIntSockIdxFromSocketId(rozofs_exp_sup_conf_t *p,int socketId)
{
   int i;


   for (i = 0;i < 2;i++)
   {
     if (p->internalSocket[i]==socketId) return (uint32_t)i;
   }
   return -1;
}



/*----------------------------------------------
**  rozofs_exp_rcvReadyInternalSock
**----------------------------------------------
**
**   receive ready function: only for
**   receiver socket. Nothing expected
**   on sending socket
**
**
**  IN :
**     not_significant : Relci instance index
**     socketId : socket Identifier
**
**  OUT : always TRUE for RUC_SOC_RECV
**        always FALSE for RUC_SOC_SEND
**
**-----------------------------------------------
*/
uint32_t rozofs_exp_rcvReadyInternalSock(void * not_significant,int socketId)
{
  rozofs_exp_sup_conf_t *p;
  uint32_t      socketIdx;

  /*
  **  Get the pointer to the timer Object
  */
  p = &rozofs_exp_lbg_ctx;
  socketIdx = rozofs_exp_getIntSockIdxFromSocketId(p,socketId);
  if (socketIdx == -1)
  {
    /*
    ** something really wrong
    */
    severe("rozofs_exp_rcvReadyInternalSock : cannot get socket reference");
    return FALSE;
  }
  if (socketIdx == RUC_SOC_SEND)
    return FALSE;
  else
    return TRUE;
}

/*----------------------------------------------
**  rozofs_exp_rcvMsgInternalSock
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
**     not_significant : Relci instance index
**     socketId : socket Identifier
**
**  OUT : always TRUE for RUC_SOC_RECV
**        always FALSE for RUC_SOC_SEND
**
**-----------------------------------------------
*/
uint32_t rozofs_exp_rcvMsgInternalSock(void * not_significant,int socketId)
{
  rozofs_exp_sup_conf_t *p;
  uint32_t      socketIdx;
  int         bytesRcvd;
  rozofs_exp_msg_t msg;

  /*
  **  Get the pointer to the timer Object
  */
  p = &rozofs_exp_lbg_ctx;
  socketIdx = rozofs_exp_getIntSockIdxFromSocketId(p,socketId);
  if (socketIdx == -1)
  {
    /*
    ** something really wrong
    */
    severe("rozofs_exp_rcvMsgInternalSock: cannot get reference of the socket");
    return FALSE;
  }

  if (socketIdx == RUC_SOC_SEND)
  {
    /*
    **  should not occur
    */
    fatal("rozofs_exp_rcvMsgInternalSock: invalid socket index");
    return FALSE;
  }
  /*
  **  Get the message 
  **
  */
  bytesRcvd = recv(socketId,
                   (char *)&msg,
                   sizeof(rozofs_exp_msg_t),
                   0);
  if (bytesRcvd != sizeof(rozofs_exp_msg_t))
  {
    /*
    **  something wrong : (BUG)
    */
    severe("rozofs_exp_rcvMsgInternalSock error on recv(): %s",strerror(errno));
    return TRUE;
  }
  /*
  **  process the message
  */
  switch ( msg.opcode)
  {
    case ROZOFS_EXPORT_GW_CONF:
    
      return rozofs_exp_process_export_gateway_conf_nb(p,&msg);
      break;
    default:
    
      severe("rozofs_exp_rcvMsgInternalSock: unsupported opcode : %d",msg.opcode);
      break;
  }

  return TRUE;
}


/*----------------------------------------------
**  rozofs_exp_xmitReadyInternalSock
**----------------------------------------------
**
**   xmit ready function: only for
**   xmit socket. Nothing expected
**   on receiving socket.
**
**
**  IN :
**     not_significant : Relci instance index
**     socketId : socket Identifier
**
**  OUT : always FALSE for RUC_SOC_RECV
**        always FALSE for RUC_SOC_SEND
**  There is not congestion on the internal socket
**
**-----------------------------------------------
*/
uint32_t rozofs_exp_xmitReadyInternalSock(void * not_significant,int socketId)
{
  rozofs_exp_sup_conf_t *p;
  uint32_t      socketIdx;

  /*
  **  Get the pointer to the timer Object
  */
  p = &rozofs_exp_lbg_ctx;
  socketIdx = rozofs_exp_getIntSockIdxFromSocketId(p,socketId);
  if (socketIdx == -1)
  {
    /*
    ** something really wrong
    */
    severe("rozofs_exp_xmitReadyInternalSock : invalid socket index");
    return FALSE;
  }

  if (socketIdx == RUC_SOC_RECV)
    return FALSE;
  else
    return FALSE;
}


/*----------------------------------------------
**  rozofs_exp_xmitEvtInternalSock
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
uint32_t rozofs_exp_xmitEvtInternalSock(void * not_significant,int socketId)
{
  return FALSE;
}
/*
**    I N T E R N A L   S O C K E T
**    CREATION/DELETION
*/


/*----------------------------------------------
**  rozofs_exp_createInternalSocket (private)
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

uint32_t rozofs_exp_createInternalSocket(rozofs_exp_sup_conf_t *p)
{
  int    ret;
  uint32_t retcode = RUC_NOK;
//  int    fileflags;


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
    severe("rozofs_exp_createInternalSocket: cannot create the socketpair: %s",strerror(errno));
    return RUC_NOK;
  }
  while (1)
  {
#if 0
    /*
    ** change socket mode to asynchronous
    */
    if((fileflags=fcntl(p->internalSocket[RUC_SOC_SEND],F_GETFL,0))==-1)
    {
      severe("rozofs_exp_createInternalSocket: moving to async. failure: %s",strerror(errno));
      break;
    }
    if(fcntl(p->internalSocket[RUC_SOC_SEND],F_SETFL,fileflags|O_NDELAY)==-1)
    {
      RUC_WARNING(errno);
      break;
    }
#endif
    /*
    ** 2 - perform the connection with the socket controller
    */
    p->intSockconnectionId[RUC_SOC_SEND]=
                                     ruc_sockctl_connect(p->internalSocket[RUC_SOC_SEND],
                                     "CNF_SOCK_XMIT",
                                      0,
                                      p,
                                      &rozofs_exp_callBack_InternalSock);
    if (p->intSockconnectionId[RUC_SOC_SEND]== NULL)
    {
      severe("rozofs_exp_createInternalSocket: cannot create bind the socket with socket controller idx: %d ",RUC_SOC_SEND);
      break;
    }
    p->intSockconnectionId[RUC_SOC_RECV]=
                 ruc_sockctl_connect(p->internalSocket[RUC_SOC_RECV],
                                     "CNF_SOCK_RECV",
                                      0,
                                      p,
                                      &rozofs_exp_callBack_InternalSock);
    if (p->intSockconnectionId[RUC_SOC_RECV]== NULL)
    {
      severe("rozofs_exp_createInternalSocket: cannot create bind the socket with socket controller idx: %d ",RUC_SOC_RECV);
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
    if (p->intSockconnectionId[RUC_SOC_RECV] != NULL)
    {
      ruc_sockctl_disconnect(p->intSockconnectionId[RUC_SOC_RECV]);
      p->intSockconnectionId[RUC_SOC_RECV] = NULL;
    }
    if (p->intSockconnectionId[RUC_SOC_SEND] != NULL)
    {
      ruc_sockctl_disconnect(p->intSockconnectionId[RUC_SOC_SEND]);
      p->intSockconnectionId[RUC_SOC_SEND] = NULL;
    }
    return RUC_NOK;
  }
  

  return RUC_OK;
}


/*----------------------------------------------
**  rozofs_exp_deleteInternalSocket (private)
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

uint32_t rozofs_exp_deleteInternalSocket(rozofs_exp_sup_conf_t *p)
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
  if (p->intSockconnectionId[RUC_SOC_RECV] != NULL)
  {
    ruc_sockctl_disconnect(p->intSockconnectionId[RUC_SOC_RECV]);
    p->intSockconnectionId[RUC_SOC_RECV] = NULL;
  }
  if (p->intSockconnectionId[RUC_SOC_SEND] != NULL)
  {
    ruc_sockctl_disconnect(p->intSockconnectionId[RUC_SOC_SEND]);
    p->intSockconnectionId[RUC_SOC_SEND] = NULL;
  }

  return RUC_OK;
}



/*----------------------------------------------
**  rozofs_exp_moduleInit (public)
**----------------------------------------------
**
**
**  IN :
*
**
**  OUT :
**    RUC_OK/RUC_NOK
**
**-----------------------------------------------
*/
uint32_t rozofs_exp_moduleInit()

{
  rozofs_exp_sup_conf_t *p;
  uint32_t      ret;
  /*
  **  Get the pointer to the timer Object
  */
  p = rozofs_exp_getObjRef();

   /*
   ** create the internal socket
   */

   ret = rozofs_exp_createInternalSocket(p);
   if (ret == RUC_OK)
   {
     rozofs_exp_ready4configuration = 1;
   }
   return ret;
}
