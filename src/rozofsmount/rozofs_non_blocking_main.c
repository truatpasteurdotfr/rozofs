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

#include <stdlib.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h> 
#include <errno.h>  
#include <stdarg.h>    
#include <string.h>  
#include <strings.h>
#include <semaphore.h>
#include <pthread.h>

#include <rozofs/core/ruc_common.h>
#include <rozofs/core/ruc_sockCtl_api.h>
#include <rozofs/core/ruc_timer_api.h>
#include <rozofs/core/ruc_trace_api.h>
#include <rozofs/core/uma_tcp_main_api.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/core/ruc_tcpServer_api.h>
#include <rozofs/core/ruc_tcp_client_api.h>
#include <rozofs/core/ppu_trace.h>
#include <rozofs/core/uma_well_known_ports_api.h>
#include <rozofs/core/ppu_trace.h>
#include <rozofs/core/af_unix_socket_generic_api.h>
#include <rozofs/core/north_lbg_api.h>
#include <rozofs/core/rozofs_tx_api.h>
#include <rozofs/rpc/eclient.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/storcli_lbg_prototypes.h>

#include "rozofs_fuse.h"

// For trace purpose
struct timeval Global_timeDay;
unsigned long long Global_timeBefore, Global_timeAfter;
static rozofs_fuse_conf_t *args_p;


pthread_t heartbeat_thrdId;

int module_test_id = 0;

uint32_t ruc_init(uint32_t test, uint16_t debug_port) {
    int ret;


    uint32_t mx_tcp_client = 10;
    uint32_t mx_tcp_server = 10;
    uint32_t mx_tcp_server_cnx = 10;
    uint32_t mx_af_unix_ctx = 8;
    uint32_t mx_lbg_north_ctx = 8;

    //#warning TCP configuration ressources is hardcoded!!
    /*
     ** trace buffer initialization
     */
    ruc_traceBufInit();
#if 0
    /*
     ** Not needed since there is already done
     ** by libUtil
     */

    /* catch the sigpipe signal for socket 
     ** connections with RELC(s) in this way when a RELC
     ** connection breaks an errno is set on a recv or send 
     **  socket primitive 
     */
    sigAction.sa_flags = SA_RESTART;
    sigAction.sa_handler = SIG_IGN; /* Mask SIGPIPE */
    if (sigaction(SIGPIPE, &sigAction, NULL) < 0) {
        exit(0);
    }
    sigAction.sa_flags = SA_RESTART;
    sigAction.sa_handler = hand; /*  */
    if (sigaction(SIGUSR1, &sigAction, NULL) < 0) {
        exit(0);
    }
#endif



    /*
     ** initialize the socket controller:
     **   4 connections per Relc and 32
     **   for: NPS, Timer, Debug, etc...
     */
    //#warning set the number of contexts for socketCtrl to 100
    ret = ruc_sockctl_init(100);
    if (ret != RUC_OK) {
        ERRFAT " socket controller init failed" ENDERRFAT
    }

    /*
     **  Timer management init
     */
    ruc_timer_moduleInit(FALSE);

    while (1) {
        /*
         **--------------------------------------
         **  configure the number of TCP connection
         **  supported
         **--------------------------------------   
         **  
         */
        ret = uma_tcp_init(mx_tcp_client + mx_tcp_server + mx_tcp_server_cnx);
        if (ret != RUC_OK) break;

        /*
         **--------------------------------------
         **  configure the number of TCP server
         **  context supported
         **--------------------------------------   
         **  
         */
        ret = ruc_tcp_server_init(mx_tcp_server);
        if (ret != RUC_OK) break;

        /*
         **--------------------------------------
         **  configure the number of TCP client
         **  context supported
         **--------------------------------------   
         **  
         */
        //     ret = ruc_tcp_clientinit(mx_tcp_client);
        //     if (ret != RUC_OK) break;   


        /*
         **--------------------------------------
         **  configure the number of AF_UNIX
         **  context supported
         **--------------------------------------   
         **  
         */
        ret = af_unix_module_init(mx_af_unix_ctx,
                32, 1024, // xmit(count,size)
                32, 1024 // recv(count,size)
                );
        if (ret != RUC_OK) break;

        /*
         ** init of th emodule that handles the load balancing group
         ** there is one load balancing group for the exportd
         ** and one load balancing group per storaged
         */
#if 1
        ret = north_lbg_module_init(mx_lbg_north_ctx);
        if (ret != RUC_OK) break;

        ret = rozofs_tx_module_init(args_p->max_transactions, // transactions count
                args_p->max_transactions, 2048, // xmit small [count,size]
                args_p->max_transactions, (1024 * 258), // xmit large [count,size]
                args_p->max_transactions, 1024, // recv small [count,size]
                args_p->max_transactions, (1024 * 258)); // recv large [count,size];  

        if (ret != RUC_OK) break;
#endif    
        break;

    }



    /*
     ** internal debug init
     */
    //ruc_debug_init();


    /*
     **--------------------------------------
     **   D E B U G   M O D U L E
     **--------------------------------------
     */
    uma_dbg_init(10, INADDR_ANY, debug_port);

    /*
     ** init of the stats module
     */
    //     rozofs_stats_init();


    return ret;
}

/*
 *_______________________________________________________________________
 */

/**
 *  This function is the entry point for setting rozofs in non-blocking mode

   @param args->ch: reference of the fuse channnel
   @param args->se: reference of the fuse session
   @param args->max_transactions: max number of transactions that can be handled in parallel
   
   @retval -1 on error
   @retval : no retval -> only on fatal error

 */
int rozofs_stat_start(void *args) {


    int ret;
    //sem_t semForEver;    /* semaphore for blocking the main thread doing nothing */
    args_p = args;

    uint16_t debug_port = args_p->debug_port;


    ret = ruc_init(FALSE, debug_port);
    if (ret != RUC_OK) {
        /*
         ** fatal error
         */
        return -1;
    }

    {
        char name[32];
        sprintf(name, "rozofsmount %d", args_p->instance);
        uma_dbg_set_name(name);
    }

    /*
     ** init of the fuse part
     */
    ret = rozofs_fuse_init(args_p->ch, args_p->se, args_p->max_transactions);
    if (ret != RUC_OK) {
        /*
         ** fatal error
         */
        return -1;
    }
    /*
     ** Perform the init with exportd--> setup of the TCP connection associated with the load balancing group
     */
    if (export_lbg_initialize((exportclt_t*) args_p->exportclt, EXPORT_PROGRAM, EXPORT_VERSION, 0) != 0) {
        severe("Cannot setup the load balancing group towards Exportd");
    }
    //#warning storcli instances are hardcoded
    if (storcli_lbg_initialize((exportclt_t*) args_p->exportclt, args_p->instance, 1, 2) != 0) {
        severe("Cannot setup the load balancing group towards StorCli");
    }
    /*
     ** main loop
     */
    while (1) {
        ruc_sockCtrl_selectWait();
    }
    ERRFAT "main() code is rotten" ENDERRFAT
}
