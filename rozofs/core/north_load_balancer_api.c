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
#include <sys/types.h> 
#include <sys/socket.h>
#include <fcntl.h> 
#include <sys/un.h>             
#include <errno.h>  
#include <rozofs/common/types.h>
#include <rozofs/common/log.h>
#include "ruc_common.h"
#include "ruc_list.h"
#include "af_unix_socket_generic_api.h"
#include "af_unix_socket_generic.h"
#include "rozofs_socket_family.h"
#include "ppu_trace.h"
#include "uma_dbg_api.h"
#include "north_lbg_timer.h"
#include "north_lbg_timer_api.h"
#include "north_lbg.h"
#include "north_lbg_api.h"
#include "af_inet_stream_api.h"


void north_lbg_entry_start_timer(north_lbg_entry_ctx_t *entry_p,uint32_t time_ms) ;
void north_lbg_entry_timeout_CBK (void *opaque);
void north_lbg_entry_stop_timer(north_lbg_entry_ctx_t *pObj);

/*__________________________________________________________________________
*/
/**
* Check if there is some pending buffer in the global pending xmit queue

  @param lbg_p: pointer to a load balancing group
  @param entry_p: pointer to an element of a load balancing group
  @param xmit_credit : number of element that can be removed from the pending xmit list
  
  @retvak none;

*/

void north_lbg_poll_xmit_queue(north_lbg_ctx_t  *lbg_p, north_lbg_entry_ctx_t  *entry_p,int xmit_credit)
{
   ruc_obj_desc_t *pnext;
   int ret;
   int i;
   void *buf_p;

   pnext = (ruc_obj_desc_t*)NULL;
   for (i = 0; i < xmit_credit; i++)
   {
   while ((buf_p = (void*) ruc_objGetNext((ruc_obj_desc_t*)&lbg_p->xmitList[0],
                                        &pnext))
               !=NULL) 
   {         
     ruc_objRemove((ruc_obj_desc_t*)buf_p);
     lbg_p->stats.xmitQueuelen--;
     ret = af_unix_generic_send_stream_with_idx(entry_p->sock_ctx_ref,buf_p);  
     if (ret ==  0)
     {
       lbg_p->stats.totalXmit++;
       continue;
     } 
     /*
     ** there is an error, the interface is down again, so requeue the buffer at the head
     ** do we need to udpate the retry counter of the buffer ??
     */  
     lbg_p->stats.totalXmitError++; 
     lbg_p->stats.xmitQueuelen++;
     ruc_objInsert(&lbg_p->xmitList[0],(ruc_obj_desc_t*) buf_p);
     return;   
   }
   return; 
   }
 }
 
/*__________________________________________________________________________
*/
/**
  Application callBack:

   Called from tcp or af_unix when a buffer has been successfully received

    
  @param userRef : pointer to a load balancer entry
 @param socket_context_ref: socket context reference
 @param bufRef : pointer to the packet buffer on which the error has been encountered
 @param err_no : errno has reported by the sendto().
 
*/
void  north_lbg_userRecvCallBack(void *userRef,uint32_t  socket_ctx_idx, void *bufRef)
{
   north_lbg_entry_ctx_t *entry_p = (north_lbg_entry_ctx_t*)userRef;
   north_lbg_ctx_t       *lbg_p   = (north_lbg_ctx_t*)entry_p->parent;
   /*
   ** update the statistics
   */
   lbg_p->stats.totalRecv++;
   /*
   ** check if there is some message to pull out from the global queue
   */
   north_lbg_poll_xmit_queue((north_lbg_ctx_t*)entry_p->parent,entry_p,1);
   /*
   ** OK now call the application
   */
   (lbg_p->userRcvCallBack)(NULL,lbg_p->index,bufRef);
}

/*__________________________________________________________________________
*/
/**
* test function that is called upon a failure on sending

 The application might use that callback if it has some other
 destination that can be used in case of failure of the current one
 If the application has no other destination to select, it is up to the
 application to release the buffer.
 

 @param userRef : pointer to a load balancer entry
 @param socket_context_ref: socket context reference
 @param bufRef : pointer to the packet buffer on which the error has been encountered
 @param err_no : errno has reported by the sendto().
 
 @retval none
*/

void  north_lbg_userDiscCallBack(void *userRef,uint32_t socket_context_ref,void *bufRef,int err_no)
{
//    int len;
    int ret;
    int8_t retry_counter;
   north_lbg_entry_ctx_t *entry_p = (north_lbg_entry_ctx_t*)userRef;
   north_lbg_ctx_t       *lbg_p   = (north_lbg_ctx_t*)entry_p->parent;
   ruc_obj_desc_t        *pnext = (ruc_obj_desc_t*)NULL;
   int up2down_transition = 0;

    /*
    ** change the state to DOWN
    */
    if (entry_p->state != NORTH_LBG_DOWN) 
    { 
      north_lbg_entry_state_change(entry_p,NORTH_LBG_DOWN);
      warning("north_lbg_userDiscCallBack->Disconnect for %d \n",socket_context_ref);
      up2down_transition = 1;
    }
    /*
    **get the pointer to the destination stored in the buffer
    */
    while (bufRef != NULL) 
    {
       /*
       ** get the retry counter of the buffer
       */
       retry_counter = ruc_buf_get_retryCounter(bufRef);
//       if ((retry_counter >= NORTH_LBG_MAX_RETRY) || (lbg_p->state == NORTH_LBG_DOWN))
       if ((retry_counter >= lbg_p->nb_entries_conf) || (lbg_p->state == NORTH_LBG_DOWN))
       {       
         /*
         ** inform the application or release the buffer
         */
         lbg_p->stats.totalXmitAborts++;
         if (lbg_p->userDiscCallBack!= NULL)
         {
         
          (lbg_p->userDiscCallBack)(NULL,lbg_p->index,bufRef, err_no); 
          break;        
         }
         /*
         ** release the buffer
         */
#warning Need to check the in_use counter of the buffer before the release
         ruc_buf_freeBuffer(bufRef); 
         break;               
       }
       /*
       ** the lbg is still up and there the retry count is not exhausted
       */
       retry_counter +=1;
       ruc_buf_set_retryCounter(bufRef,retry_counter);
       /*
       ** resend by selecting a new destination
       */
       lbg_p->stats.totalXmitRetries++;
       north_lbg_send(lbg_p->index,bufRef);
       break;
    }
    /*
    ** OK, now go the buffer that might be queued on that entry and do the same
    */    
    while ((bufRef = (void*) ruc_objGetNext((ruc_obj_desc_t*)&entry_p->xmitList,
                                         &pnext))
                !=NULL) 
    { 
      /*
      ** remove it from the list because it might be queued afterwards on a new queue
      */        
      ruc_objRemove((ruc_obj_desc_t*)bufRef);
      while (1) 
      {
         /*
         ** get the retry counter of the buffer
         */
         retry_counter = ruc_buf_get_retryCounter(bufRef);
//         if ((retry_counter >= NORTH_LBG_MAX_RETRY) || (lbg_p->state == NORTH_LBG_DOWN))
         if ((retry_counter >= lbg_p->nb_entries_conf) || (lbg_p->state == NORTH_LBG_DOWN))
         {
           /*
           ** inform the application or release the buffer
           */
           lbg_p->stats.totalXmitAborts++;
           if (lbg_p->userDiscCallBack!= NULL)
           {
            (lbg_p->userDiscCallBack)(NULL,lbg_p->index,bufRef, err_no); 
            break;        
           }
           /*
           ** release the buffer
           */
           ruc_buf_freeBuffer(bufRef); 
           break;               
         }
         /*
         ** the lbg is still up and there the retry count is not exhausted
         */
         retry_counter +=1;
         ruc_buf_set_retryCounter(bufRef,retry_counter);
         /*
         ** resend by selecting a new destination
         */
         lbg_p->stats.totalXmitRetries++;
         north_lbg_send(lbg_p->index,bufRef);
         break;
      }   
    }   
    /*
    ** do the reconnect on UP 2 DOWN transition only
    */
  if (up2down_transition) 
  {
    entry_p->stats.totalConnectAttempts++;
    ret = af_unix_sock_client_reconnect(socket_context_ref);
    if (ret < 0)
    {
 //  printf("north_lbg_userDiscCallBack->fatal error on reconnect\n");
      north_lbg_entry_start_timer(entry_p,4); 
    }
  }
}

/*__________________________________________________________________________
*/
/**
* that callback is called upon the successful transmission of a buffer
  Depending on the inuse value of the buffer, the application can
  either release the message or queue it in its local xmit queue.
  The main purpose of queueing the message is to address the case of
  a disconnection of the remote end in order to re-balance the buffer
  on another equivalent destinatio
 

 @param userRef : pointer to a load balancer entry
 @param socket_context_ref: socket context reference
 @param bufRef : pointer to the packet buffer on which the error has been encountered
  
 @retval none
*/
void  north_lbg_userXmiDoneCallBack(void *userRef,uint32_t socket_context_ref,void *bufRef)
{
    int8_t inuse;
    
   north_lbg_entry_ctx_t *entry_p = (north_lbg_entry_ctx_t*)userRef;

    /*
    ** check the inuse value of the buffer, if inuse is 1, then release it
    */
    inuse = ruc_buf_inuse_get(bufRef);
    if (inuse < 0)
    {
#warning  inuse MUST never be negative so EXIT !!!!!
     exit(0);          
    }
    if (inuse == 1) 
    {
       ruc_buf_freeBuffer(bufRef);  
    } 
    else
    {
      /*
      ** queue it to the local xmit list of the entry 
      */
      ruc_objInsertTail((ruc_obj_desc_t*)&entry_p->xmitList,(ruc_obj_desc_t*)bufRef);    
    } 
}

/*__________________________________________________________________________
*/
/**
*   Application connect callback
    The retcode contains the result of the connect() operation started 
    in asynchronous mode. That value is one of the following:
    
    - RUC_OK : success
    - RUC_NOK : failure(see errnum for details

 @param userRef : pointer to a load balancer entry
 @param socket_context_ref : index of the socket context
 @param retcode : connection status
 @param errnum : value of the errno in case of error
 
 @retval none
*/
void north_lbg_connect_cbk (void *userRef,uint32_t socket_context_ref,int retcode,int errnum)
{
   north_lbg_entry_ctx_t *entry_p = (north_lbg_entry_ctx_t*)userRef;
   uint8_t fake_buf[16];
   int len;
   int status;
   /*
   ** in case of success starts sending the messages
   */
   if (retcode != RUC_OK)
   {
//     printf("error on connect() %s\n",strerror(errnum));
     /*
     ** restart the delay timer and then retry
     */
     north_lbg_entry_start_timer(entry_p,4);
     return;    
   }
   /**
   * do a fake read to be sure that the socket is up
   */
   af_unix_ctx_generic_t *sock_p = af_unix_getObjCtx_p(socket_context_ref);
    
   status = af_unix_recv_stream_sock_recv(sock_p,fake_buf,2,MSG_PEEK,&len);
   switch(status)
   {
     case RUC_OK:   
     case RUC_WOULDBLOCK:
     case RUC_PARTIAL:
      north_lbg_entry_stop_timer(entry_p);
      sock_p->stats.totalUpDownTransition++;
      north_lbg_entry_state_change(entry_p,NORTH_LBG_UP);
      /*
      ** attempt the fill the xmit queue of that entry
      */
      north_lbg_poll_xmit_queue((north_lbg_ctx_t*)entry_p->parent,entry_p,NORTH_LBG_MAX_XMIT_ON_UP_TRANSITION+2);
      
//      entry_p->state = NORTH_LBG_UP ;
//      entry_p->stats.totalUpDownTransition++;
      printf("Successful reconnection!!!\n");  
      return;
      
     case RUC_DISC:
     default:
       /*
       ** still deconnected-> remove from socket controller and restart the timer
       */
       af_unix_disconnect_from_socketCtrl(socket_context_ref);
        north_lbg_entry_start_timer(entry_p,4);
   }
}

/*
**____________________________________________________
*/
/*
    Timeout call back associated with a transaction

@param     :  tx_p : pointer to the transaction context
*/

void north_lbg_entry_timeout_CBK (void *opaque)
{
  int ret;  
  north_lbg_entry_ctx_t *entry_p = (north_lbg_entry_ctx_t*)opaque;
//  entry_p->rpc_guard_timer_flg = TRUE;

   entry_p->stats.totalConnectAttempts++;
   ret = af_unix_sock_client_reconnect(entry_p->sock_ctx_ref);
   if (ret < 0)
   {
//      printf("north_lbg_entry_timeout_CBK-->fatal error on reconnect\n");
      /*
      ** restart the timer
      */
      north_lbg_entry_start_timer(entry_p,4);
   }
}
/*
**____________________________________________________
*/
/*
  stop the guard timer associated with the transaction

@param     :  entry_p : pointer to the transaction context
@retval   : none
*/

void north_lbg_entry_stop_timer(north_lbg_entry_ctx_t *pObj)
{
 
//  pObj->rpc_guard_timer_flg = FALSE;
  north_lbg_tmr_stop(&pObj->rpc_guard_timer); 
}

/*
**____________________________________________________
*/
/*
  start the guard timer associated with the transaction

@param     : entry_p : pointer to the transaction context
@param     : uint32_t  : delay in seconds (??)
@retval   : none
*/
void north_lbg_entry_start_timer(north_lbg_entry_ctx_t *entry_p,uint32_t time_ms) 
{
 uint8_t slot;
  /*
  **  remove the timer from its current list
  */
  slot = NORTH_LBG_TMR_SLOT0;

//  entry_p->rpc_guard_timer_flg = FALSE;
  north_lbg_tmr_stop(&entry_p->rpc_guard_timer);
  north_lbg_tmr_start(slot,
                  &entry_p->rpc_guard_timer,
		  time_ms*1000,
                  north_lbg_entry_timeout_CBK,
		  (void*) entry_p);

}


/*__________________________________________________________________________
*/
/**
*  create a north load balancing object

  @param @name : name of the load balancer
  @param  basename_p : Base name of the remote sunpath
  @param @family of the load balancer
  @param first_instance: index of the first instance
  @param nb_instances: number of instances
  
  @retval >= reference of the load balancer object
  @retval < 0 error (out of context ??)
*/
int north_lbg_create_af_unix(char *name,char *basename_p,int family,int first_instance,int  nb_instances,af_unix_socket_conf_t *usr_conf_p)
{
  char sun_path[128];
  char nickname[128];
  north_lbg_ctx_t  *lbg_p;
  int    i;
  north_lbg_entry_ctx_t *entry_p;
  af_unix_socket_conf_t *conf_p; 
  
  if (nb_instances == 0)
  {
    /*
    ** no instances!!
    */
    RUC_WARNING(nb_instances);
    return -1;   
  }
  if (nb_instances >= NORTH__LBG_MAX_ENTRY)
  {
    /*
    ** to many instances!!
    */
    RUC_WARNING(nb_instances);
    return -1;   
  }  
  /*
  ** allocate a load balancer context
  */
  lbg_p = north_lbg_alloc();
  if (lbg_p == NULL) 
  {
    /*
    ** out of context
    */
    RUC_WARNING(-1);
    return -1; 
  }
  lbg_p->family = family;
  lbg_p->nb_entries_conf = nb_instances;
  strcpy(lbg_p->name,name);
  /*
  ** save the configuration of the lbg
  */
  conf_p = &lbg_p->lbg_conf;
  memcpy(conf_p,usr_conf_p,sizeof(af_unix_socket_conf_t));

  /*
  ** install the load balancer callback but keep in the context the 
  ** user callback for deconnection
  */
  lbg_p->userDiscCallBack       = conf_p->userDiscCallBack;
  conf_p->userDiscCallBack    = north_lbg_userDiscCallBack;
  lbg_p->userRcvCallBack      = conf_p->userRcvCallBack;
  conf_p->userRcvCallBack     = north_lbg_userRecvCallBack;
  conf_p->userConnectCallBack = north_lbg_connect_cbk;
  conf_p->userXmitDoneCallBack = north_lbg_userXmiDoneCallBack;
  
  entry_p = lbg_p->entry_tb;
  for (i = 0; i < nb_instances ; i++,entry_p++)
  {
     sprintf(sun_path,"%s_inst_%d",basename_p,i+first_instance);
     sprintf(nickname,"%s_%d",name,i+first_instance);
     conf_p->instance_id = i+first_instance;
     conf_p->userRef     = entry_p;
     entry_p->sock_ctx_ref = af_unix_sock_client_create(nickname,sun_path,conf_p); 
     if (entry_p->sock_ctx_ref >= 0)  
     {
       north_lbg_entry_start_timer(entry_p,4);
       north_lbg_entry_state_change(entry_p,NORTH_LBG_DOWN);
//       entry_p->state = NORTH_LBG_DOWN; 
     }  
  }
  return (lbg_p->index);
}





/*__________________________________________________________________________
*/
/**
*  create a north load balancing object with AF_INET

  @param @name : name of the load balancer
  @param  basename_p : Base name of the remote sunpath
  @param @family of the load balancer
  
  @retval >= reference of the load balancer object
  @retval < 0 error (out of context ??)
*/
int north_lbg_create_af_inet(char *name,
                             uint32_t src_ipaddr_host,
                             uint16_t src_port_host,
                             north_remote_ip_list_t *remote_ip_p,
                             int family,int  nb_instances,af_unix_socket_conf_t *usr_conf_p)
{
  north_lbg_ctx_t  *lbg_p;
  int    i;
  north_lbg_entry_ctx_t *entry_p;  
  af_unix_socket_conf_t *conf_p; 
   
  if (nb_instances == 0)
  {
    /*
    ** no instances!!
    */
    RUC_WARNING(nb_instances);
    return -1;   
  }
  if (nb_instances >= NORTH__LBG_MAX_ENTRY)
  {
    /*
    ** to many instances!!
    */
    RUC_WARNING(nb_instances);
    return -1;   
  }  
  /*
  ** allocate a load balancer context
  */
  lbg_p = north_lbg_alloc();
  if (lbg_p == NULL) 
  {
    /*
    ** out of context
    */
    RUC_WARNING(-1);
    return -1; 
  }
  /*
  ** save the configuration of the lbg
  */
  conf_p = &lbg_p->lbg_conf;
  memcpy(conf_p,usr_conf_p,sizeof(af_unix_socket_conf_t));
  
  lbg_p->family = family;
  lbg_p->nb_entries_conf = nb_instances;
  strcpy(lbg_p->name,name);
  /*
  ** install the  callbacks of the load balancer
  */
  /*
  ** install the load balancer callback but keep in the context the 
  ** user callback for deconnection
  */
  lbg_p->userDiscCallBack     = conf_p->userDiscCallBack;
  conf_p->userDiscCallBack    = north_lbg_userDiscCallBack;
  lbg_p->userRcvCallBack      = conf_p->userRcvCallBack;
  conf_p->userRcvCallBack     = north_lbg_userRecvCallBack;

  conf_p->userConnectCallBack = north_lbg_connect_cbk;
  conf_p->userXmitDoneCallBack = north_lbg_userXmiDoneCallBack;

  
  entry_p = lbg_p->entry_tb;
  for (i = 0; i < nb_instances ; i++,entry_p++,remote_ip_p++)
  {

     conf_p->userRef     = entry_p;
     entry_p->sock_ctx_ref = af_inet_sock_client_create(name,
                                                     src_ipaddr_host,
                                                     src_port_host,
                                                     remote_ip_p->remote_ipaddr_host,
                                                     remote_ip_p->remote_port_host,
                                                     conf_p); 
     if (entry_p->sock_ctx_ref >= 0)  
     {
       north_lbg_entry_start_timer(entry_p,4);
       north_lbg_entry_state_change(entry_p,NORTH_LBG_DOWN);
//       entry_p->state = NORTH_LBG_DOWN; 
     }  
  }
  return (lbg_p->index);
}




/**
*  API that provide the current state of a load balancing Group

 @param lbg_idx : index of the load balancing group
 
 @retval    NORTH_LBG_DEPENDENCY : lbg is idle or the reference is out of range
 @retval    NORTH_LBG_UP : at least one connection is UP
 @retval    NORTH_LBG_DOWN : all the connection are down
*/
int north_lbg_get_state(int lbg_idx)
{

  north_lbg_ctx_t  *lbg_p;


  lbg_p = north_lbg_getObjCtx_p(lbg_idx);
  if (lbg_p == NULL) 
  {
    RUC_WARNING(-1);
    return NORTH_LBG_DEPENDENCY;
  }
  return lbg_p->state;

}
  
  
 /**
*  API to allocate a  load balancing Group context with no configuration 
  
  Once the context is allocated, the state of the object is set to NORTH_LBG_DEPENDENCY.

 @param none
 
  @retval >= reference of the load balancer object
  @retval < 0 error (out of context ??)
*/
int north_lbg_create_no_conf()
{

  north_lbg_ctx_t  *lbg_p;
   
  /*
  ** allocate a load balancer context
  */
  lbg_p = north_lbg_alloc();
  if (lbg_p == NULL) 
  {
    /*
    ** out of context
    */
    RUC_WARNING(-1);
    return -1; 
  }

  lbg_p->state  = NORTH_LBG_DEPENDENCY;
  return (lbg_p->index);
}
 
/*__________________________________________________________________________
*/ 
 /**
*  API to configure a load balancing group.
   The load balancing group must have been created previously with north_lbg_create_no_conf() 
  
 @param none
 
  @retval >= reference of the load balancer object
  @retval < 0 error (out of context ??)
*/
int north_lbg_configure_af_inet(int lbg_idx,char *name,
                                uint32_t src_ipaddr_host,
                                uint16_t src_port_host,
                                north_remote_ip_list_t *remote_ip_p,
                                int family,int  nb_instances,af_unix_socket_conf_t *usr_conf_p)
{
  north_lbg_ctx_t  *lbg_p;
  int    i;
  north_lbg_entry_ctx_t *entry_p;
  
  af_unix_socket_conf_t *conf_p;
  
  lbg_p = north_lbg_getObjCtx_p(lbg_idx);
  if (lbg_p == NULL) 
  {
    RUC_WARNING(-1);
    return -1;
  }
    
  conf_p = &lbg_p->lbg_conf;
  memcpy(conf_p,usr_conf_p,sizeof(af_unix_socket_conf_t));

  if (lbg_p->state != NORTH_LBG_DEPENDENCY)
  {
    RUC_WARNING(-1);
    return -1;  
  }
  if (nb_instances == 0)
  {
    /*
    ** no instances!!
    */
    RUC_WARNING(nb_instances);
    return -1;   
  }
  if (nb_instances >= NORTH__LBG_MAX_ENTRY)
  {
    /*
    ** to many instances!!
    */
    RUC_WARNING(nb_instances);
    return -1;   
  }  
  /*
  ** restore the init state
  */
  lbg_p->state  = NORTH_LBG_DOWN;
  
  lbg_p->family = family;
  lbg_p->nb_entries_conf = nb_instances;
  strcpy(lbg_p->name,name);
  /*
  ** install the  callbacks of the load balancer
  */
  /*
  ** install the load balancer callback but keep in the context the 
  ** user callback for deconnection
  */
  lbg_p->userDiscCallBack     = conf_p->userDiscCallBack;
  conf_p->userDiscCallBack    = north_lbg_userDiscCallBack;
  lbg_p->userRcvCallBack      = conf_p->userRcvCallBack;
  conf_p->userRcvCallBack     = north_lbg_userRecvCallBack;

  conf_p->userConnectCallBack = north_lbg_connect_cbk;
  conf_p->userXmitDoneCallBack = north_lbg_userXmiDoneCallBack;

  
  entry_p = lbg_p->entry_tb;
  for (i = 0; i < nb_instances ; i++,entry_p++,remote_ip_p++)
  {

     conf_p->userRef     = entry_p;
     entry_p->sock_ctx_ref = af_inet_sock_client_create(name,
                                                     src_ipaddr_host,
                                                     src_port_host,
                                                     remote_ip_p->remote_ipaddr_host,
                                                     remote_ip_p->remote_port_host,
                                                     conf_p); 
     if (entry_p->sock_ctx_ref >= 0)  
     {
       north_lbg_entry_start_timer(entry_p,4);
       north_lbg_entry_state_change(entry_p,NORTH_LBG_DOWN);
//       entry_p->state = NORTH_LBG_DOWN; 
     }  
  }
  return (lbg_p->index);

}



/*__________________________________________________________________________
*/
/**
*  create a north load balancing object with AF_INET

  @param lbg_idx : reference of the load balancing group
  @param buf_p: pointer to the buffer to send
  
  retval 0 : success
  retval -1 : error
*/
int north_lbg_send(int  lbg_idx,void *buf_p)
{                             
  north_lbg_ctx_t  *lbg_p;
  int entry_idx;
  int ret = 0;


  lbg_p = north_lbg_getObjCtx_p(lbg_idx);
  if (lbg_p == NULL) 
  {
    RUC_WARNING(-1);
    return -1;
  }
reloop:
  /*
  ** we have the context, search for a valid entry
  */
  if (lbg_p->state == NORTH_LBG_DOWN)
  {
    /*
    ** Insert the buffer in the global pending list of the load balancing froup
    */
    ruc_objInsertTail((ruc_obj_desc_t*)&lbg_p->xmitList[0],(ruc_obj_desc_t*)buf_p);  
    /*
    ** update statistics
    */
    lbg_p->stats.xmitQueuelen++;     
    return 0;  
  }
  /*
  ** check if the main queue is empty if the queue is not empty just queue our message
  ** at the tail of the load balancer main queue
  */
  if (!ruc_objIsEmptyList((ruc_obj_desc_t*)&lbg_p->xmitList[0]))
  {
     /*
     ** queue the message at the tail
     */
     ruc_objInsertTail((ruc_obj_desc_t*)&lbg_p->xmitList[0],(ruc_obj_desc_t*)buf_p);  
    /*
    ** update statistics
    */
    lbg_p->stats.xmitQueuelen++;     
    return 0;  
  }
  /*
  ** OK there is at least one entry that is free, so get the next valid entry
  */
  entry_idx = north_lbg_get_next_valid_entry(lbg_p);
  if (entry_idx < 0)
  {
    /*
    ** that situation must not occur since there is at leat one entry that is UP!!!!
    */
//    RUC_WARNING(-1);
    return -1;    
  }
  /*
  ** That's fine, get the pointer to the entry in order to get its socket context reference
  */
  north_lbg_entry_ctx_t  *entry_p = &lbg_p->entry_tb[entry_idx];
  ret = af_unix_generic_send_stream_with_idx(entry_p->sock_ctx_ref,buf_p); 
  if (ret == 0)
  {
    lbg_p->stats.totalXmit++; 
    return 0; 
  } 
  /*
  ** retry on a next entry
  ** we might need to update the retry counter of the buffer ??
  */
  lbg_p->stats.totalXmitError++; 
  goto reloop;
  
  return 0;
}
