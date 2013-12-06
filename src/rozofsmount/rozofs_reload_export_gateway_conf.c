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

#include <pthread.h>

#include <rozofs/rpc/eclient.h>

#include "rozofs_reload_export_gateway_conf.h"
#include "rozofs_export_gateway_conf_non_blocking.h"

rozofs_conf_ctx_t rozofs_conf_ctx;      /**< statistics associated with exportd configuration polling and reload */


/*__________________________________________________________________________
 */
/**  Periodic thread whose aim is to detect a change in the configuration
    of the export and then to reload the lastest configuration of the exportd
    
 *
 * @param exportd_context_p: pointer to the exportd Master data structure
 */
#define CONF_CONNECTION_THREAD_TIMESPEC  4

void *rozofs_exportd_config_supervision_thread(void *exportd_context_p) {

 exportclt_t * clt = (exportclt_t*) exportd_context_p;
 ep_gateway_t  arg_poll;
 epgw_status_ret_t  *ret_poll_p;
 ep_gw_gateway_configuration_ret_t   *ret_conf_p;
 rozofs_conf_ctx_t *rozofs_conf_ctx_p = &rozofs_conf_ctx;
 int status = -1;
 int retry = 0;
// int loop=1;
 
 struct timespec ts = {CONF_CONNECTION_THREAD_TIMESPEC, 0};
 pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
 
 /*
 ** No synchro with the non-blocking side, just wait for a while for the initialization to finish
 */
 //while(loop==1)
 {
   nanosleep(&ts, NULL); 
 }
 
 info("Rozofs: exportd configuration supervision thread started !");
 /*
 ** clear the statistics array
 */
 memset(&rozofs_conf_ctx,0,sizeof(rozofs_conf_ctx_t));
 
 for(;;)
 {
    ret_poll_p = NULL;
    ret_conf_p = NULL;
    
    /*
    ** step 1: poll the state of the current configuration of the exportd
    ** For that purpose the storlci provides the current hash value of the
    configuration
    */

    arg_poll.eid = 0;  /* NS*/
    arg_poll.hash_config  = exportd_configuration_file_hash;
    arg_poll.nb_gateways  = 0; /* NS */
    arg_poll.gateway_rank = 0; /* NS */

    status = -1;
    retry = 0;   
    ret_poll_p = NULL; 
    ROZOFS_CONF_STATS_INC(rozofs_conf_ctx_p,poll_counter);
    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret_poll_p = ep_poll_conf_1(&arg_poll, clt->rpcclt.client)))) {

        /*
        ** release the sock if already configured to avoid losing fd descriptors
        */
        rpcclt_release(&clt->rpcclt);

        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE, 0, clt->timeout) != 0) {
            rpcclt_release(&clt->rpcclt);
            ROZOFS_CONF_STATS_NOK(rozofs_conf_ctx_p,poll_counter);
            errno = EPROTO;
        }
    }
    /*
    ** Check if poll has been successful
    */
    if (ret_poll_p == 0) {
        errno = EPROTO;
        goto out;
    } 
    if (ret_poll_p->status_gw.status == EP_FAILURE) {
        ROZOFS_CONF_STATS_NOK(rozofs_conf_ctx_p,poll_counter);
        goto out;
    } 
    if (ret_poll_p->status_gw.status == EP_SUCCESS) {
        /*
        ** the configuration is synced, nothing more to do
        */
        ROZOFS_CONF_STATS_OK(rozofs_conf_ctx_p,poll_counter);
        rozofs_conf_ctx_p->conf_state = ROZOFS_CONF_SYNCED;
        goto out;
    } 
    if (ret_poll_p->status_gw.status != EP_NOT_SYNCED) {
        /*
        ** unexpected return code !!
        */
        ROZOFS_CONF_STATS_NOK(rozofs_conf_ctx_p,poll_counter);

        goto out;
    }
    ROZOFS_CONF_STATS_OK(rozofs_conf_ctx_p,poll_counter);
    rozofs_conf_ctx_p->conf_state = ROZOFS_CONF_NOT_SYNCED;
    /*
    ** OK, it looks like that the configuration is out of sync
    ** so reload it
    */
    status = -1;
    retry = 0; 
    ret_conf_p = NULL;   
    ROZOFS_CONF_STATS_INC(rozofs_conf_ctx_p,conf_counter);
    while ((retry++ < clt->retries) &&
            (!(clt->rpcclt.client) ||
            !(ret_conf_p = ep_conf_expgw_1(&clt->root, clt->rpcclt.client)))) {

        /*
        ** release the sock if already configured to avoid losing fd descriptors
        */
        rpcclt_release(&clt->rpcclt);
  
        if (rpcclt_initialize
                (&clt->rpcclt, clt->host, EXPORT_PROGRAM, EXPORT_VERSION,
                ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE, 0, clt->timeout) != 0) {
            rpcclt_release(&clt->rpcclt);
            ROZOFS_CONF_STATS_NOK(rozofs_conf_ctx_p,conf_counter);
            errno = EPROTO;
        }
    }
    /*
    ** Check if poll has been successful
    */
    if (ret_conf_p == 0) {
        errno = EPROTO;
        goto out;
    } 
    if (ret_conf_p->status_gw.status == EP_FAILURE) {
        errno = ret_conf_p->status_gw.ep_gateway_configuration_ret_t_u.error;
        ROZOFS_CONF_STATS_NOK(rozofs_conf_ctx_p,conf_counter);
        goto out;
    } 
    if (ret_conf_p->status_gw.status != EP_SUCCESS) {
        /*
        ** unexpected return code !!
        */
        ROZOFS_CONF_STATS_NOK(rozofs_conf_ctx_p,conf_counter);
        goto out;
    }
    
    /**
    * submit the message to the non blocking side and wait for the execution status
    */   
    status = rozofs_exp_reload_export_gateway_conf(&ret_conf_p->status_gw.ep_gateway_configuration_ret_t_u.config);
    if (status == 0)
    {
    /*
      ** all is fine
      */
      ROZOFS_CONF_STATS_OK(rozofs_conf_ctx_p,conf_counter);
      rozofs_conf_ctx_p->conf_state = ROZOFS_CONF_SYNCED;
      exportd_configuration_file_hash = ret_conf_p->status_gw.ep_gateway_configuration_ret_t_u.config.hdr.configuration_indice;      


    }
out: 
    if (ret_poll_p != NULL) xdr_free((xdrproc_t) xdr_epgw_status_ret_t, (char *) ret_poll_p);
    if (ret_conf_p != NULL) xdr_free((xdrproc_t) xdr_ep_gw_gateway_configuration_ret_t, (char *) ret_conf_p);
    nanosleep(&ts, NULL); 
 }
}
       
/*__________________________________________________________________________
*/

/**
 *  API to start the exportd configuration supervision thread
 *
    The goal of that thread is to poll the master exportd for checking
    any change in the configuration and to reload the configuration
    when there is a change
    
 @param clt: pointer to the context that contains the information relation to the exportd local config.
 
 @retval 0 on success
 @retval -1 on error
 */
int rozofs_start_exportd_config_supervision_thread(exportclt_t * clt) {

     pthread_t thread;
     int status = -1;
     
     if ((errno = pthread_create(&thread, NULL, rozofs_exportd_config_supervision_thread, clt)) != 0) {
         severe("can't create exportd conf. supervision thread: %s", strerror(errno));
         return status;
     }
     return 0;

}
