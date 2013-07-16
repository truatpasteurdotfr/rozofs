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

/* need for crypt */
#define _XOPEN_SOURCE 500

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
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
#include <config.h>

#include <rozofs/rozofs.h>
#include <rozofs/rozofs_debug_ports.h>
#include <rozofs/common/log.h>
#include <rozofs/common/profile.h>
#include <rozofs/core/ruc_common.h>
#include <rozofs/core/ruc_sockCtl_api.h>
#include <rozofs/core/ruc_timer_api.h>
#include <rozofs/core/uma_tcp_main_api.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/core/ruc_tcpServer_api.h>
#include <rozofs/core/ruc_tcp_client_api.h>
#include <rozofs/core/uma_well_known_ports_api.h>
#include <rozofs/core/af_unix_socket_generic_api.h>
#include <rozofs/core/north_lbg_api.h>
#include <rozofs/core/ruc_list.h>
#include <rozofs/core/af_unix_socket_generic_api.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/epproto.h>

#include "storaged_nblock_init.h"

DECLARE_PROFILING(spp_profiler_t);

/*
 **_________________________________________________________________________
 *      PUBLIC FUNCTIONS
 **_________________________________________________________________________
 */


static char localBuf[8192];
void show_uptime(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = localBuf;
    time_t elapse;
    int days, hours, mins, secs;

    // Compute uptime for storaged process
    elapse = (int) (time(0) - gprofiler.uptime);
    days = (int) (elapse / 86400);
    hours = (int) ((elapse / 3600) - (days * 24));
    mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
    secs = (int) (elapse % 60);
    pChar += sprintf(pChar, "uptime =  %d days, %d:%d:%d\n", days, hours, mins, secs);
    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}      



#define sp_display_probe(the_profiler, the_probe)\
    {\
        uint64_t rate;\
        uint64_t cpu;\
        if ((the_profiler.the_probe[P_COUNT] == 0) || (the_profiler.the_probe[P_ELAPSE] == 0) ){\
            cpu = rate = 0;\
        } else {\
            rate = (the_profiler.the_probe[P_COUNT] * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
            cpu = the_profiler.the_probe[P_ELAPSE] / the_profiler.the_probe[P_COUNT];\
        }\
        pChar += sprintf(pChar, " %-16s | %-12"PRIu64" | %-12"PRIu64" | %-12"PRIu64" |\n",\
                #the_probe, the_profiler.the_probe[P_COUNT], \
                rate, cpu);\
    }

#define sp_display_io_probe(the_profiler, the_probe)\
    {\
        uint64_t rate;\
        uint64_t cpu;\
        uint64_t throughput;\
        if ((the_profiler.the_probe[P_COUNT] == 0) || (the_profiler.the_probe[P_ELAPSE] == 0) ){\
            cpu = rate = throughput = 0;\
        } else {\
            rate = (the_profiler.the_probe[P_COUNT] * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
            cpu = the_profiler.the_probe[P_ELAPSE] / the_profiler.the_probe[P_COUNT];\
            throughput = (the_profiler.the_probe[P_BYTES] / 1024 /1024 * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
        }\
        pChar += sprintf(pChar, " %-16s | %-12"PRIu64" | %-12"PRIu64" | %-12"PRIu64" | %-12"PRIu64" | %-12"PRIu64"     |\n",\
                 #the_probe, the_profiler.the_probe[P_COUNT], \
                rate, cpu, the_profiler.the_probe[P_BYTES], throughput);\
    }

static void show_profile_storaged_master_display(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = localBuf;

    time_t elapse;
    int days, hours, mins, secs;


    // Compute uptime for storaged process
    elapse = (int) (time(0) - gprofiler.uptime);
    days = (int) (elapse / 86400);
    hours = (int) ((elapse / 3600) - (days * 24));
    mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
    secs = (int) (elapse % 60);


    // Print general profiling values for storaged
    pChar += sprintf(pChar, "storaged: %s - %"PRIu16" IO process(es),"
            " uptime: %d days, %d:%d:%d\n\n",
            gprofiler.vers, gprofiler.nb_io_processes, days, hours, mins, secs);

    // Print header for operations profiling values for storaged
    pChar += sprintf(pChar, " %-16s | %-12s | %-12s | %-12s |\n", "OP",
            "CALL", "RATE(msg/s)", "CPU(us)");
    pChar += sprintf(pChar, "------------------+--------------+--------------+--------------+\n");

    // Print master storaged process profiling values
    sp_display_probe(gprofiler, stat);
    sp_display_probe(gprofiler, ports);
    sp_display_probe(gprofiler, remove);
    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}

static void show_profile_storaged_io_display(char * argv[], uint32_t tcpRef, void *bufRef) {

    char *pChar = localBuf;

    time_t elapse;
    int days, hours, mins, secs;

    // Compute uptime for storaged process
    elapse = (int) (time(0) - gprofiler.uptime);
    days = (int) (elapse / 86400);
    hours = (int) ((elapse / 3600) - (days * 24));
    mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
    secs = (int) (elapse % 60);


    pChar += sprintf(pChar, "GPROFILER version %s uptime =  %d days, %d:%d:%d\n\n", gprofiler.vers, days, hours, mins, secs);


    // Print header for operations profiling values for storaged
    pChar += sprintf(pChar, " %-16s | %-12s | %-12s | %-12s | %-12s | %-16s |\n", "OP",
            "CALL", "RATE(msg/s)", "CPU(us)", "COUNT(B)", "THROUGHPUT(MB/s)");
    pChar += sprintf(pChar, "------------------+--------------+--------------+--------------+--------------+------------------+\n");


    // Print master storaged process profiling values
    sp_display_io_probe(gprofiler, read);
    sp_display_io_probe(gprofiler, write);
    sp_display_io_probe(gprofiler, truncate);
    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}



// For trace purpose
struct timeval Global_timeDay;
unsigned long long Global_timeBefore, Global_timeAfter;


/*
 **______________________________________________________________________________
 */

/**
 *  Convert a hostname into an IP v4 address in host format 

@param host : hostname
@param ipaddr_p : return IP V4 address arreay

@retval 0 on success
@retval -1 on error (see errno faor details
 */
static int host2ip(char *host, uint32_t *ipaddr_p) {
    struct hostent *hp;
    /*
     ** get the IP address of the storage node
     */
    if ((hp = gethostbyname(host)) == 0) {
        severe("gethostbyname failed for host : %s, %s", host,
                strerror(errno));
        return -1;
    }
    bcopy((char *) hp->h_addr, (char *) ipaddr_p, hp->h_length);
    *ipaddr_p = ntohl(*ipaddr_p);
    return 0;

}

/*
 **
 */

void fdl_debug_loop(int line) {
    int loop = 1;

    return;
    while (loop) {
        sleep(5);
        info("Fatal error on nb thread create (line %d) !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! ", line);

    }


}

uint32_t ruc_init(uint32_t test, storaged_start_conf_param_t *arg_p) {
    int ret = RUC_OK;


    uint32_t mx_tcp_client = 2;
    uint32_t mx_tcp_server = 2;
    uint32_t mx_tcp_server_cnx = 10;
    uint32_t local_ip = INADDR_ANY;

    if (arg_p->hostname[0] != 0) {
        host2ip(arg_p->hostname, &local_ip);
    }

    //#warning TCP configuration ressources is hardcoded!!
    /*
     ** init of the system ticker
     */
    rozofs_init_ticker();
    /*
     ** trace buffer initialization
     */
    ruc_traceBufInit();
#if 1
    /*
     ** Not needed since there is already done
     ** by libUtil
     */

    /* catch the sigpipe signal for socket 
     ** connections with RELC(s) in this way when a RELC
     ** connection breaks an errno is set on a recv or send 
     **  socket primitive 
     */
    struct sigaction sigAction;

    sigAction.sa_flags = SA_RESTART;
    sigAction.sa_handler = SIG_IGN; /* Mask SIGPIPE */
    if (sigaction(SIGPIPE, &sigAction, NULL) < 0) {
        exit(0);
    }
#if 0
    sigAction.sa_flags = SA_RESTART;
    sigAction.sa_handler = hand; /*  */
    if (sigaction(SIGUSR1, &sigAction, NULL) < 0) {
        exit(0);
    }
#endif
#endif

    /*
     ** initialize the socket controller:
     **   for: NPS, Timer, Debug, etc...
     */
    //#warning set the number of contexts for socketCtrl to 256
    ret = ruc_sockctl_init(16);
    if (ret != RUC_OK) {
        fdl_debug_loop(__LINE__);
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
        if (ret != RUC_OK) {
            fdl_debug_loop(__LINE__);
            break;
        }

        /*
         **--------------------------------------
         **  configure the number of TCP server
         **  context supported
         **--------------------------------------   
         **  
         */
        ret = ruc_tcp_server_init(mx_tcp_server);
        if (ret != RUC_OK) {
            fdl_debug_loop(__LINE__);
            break;
        }
        /*
         **--------------------------------------
         **   D E B U G   M O D U L E
         **--------------------------------------
         */

        uma_dbg_init(10, local_ip, arg_p->debug_port);

        {
            char name[256];
            if (arg_p->instance_id == 0) {
                sprintf(name, "storaged %s ", arg_p->hostname);
            } else {
                sprintf(name, "stor_io%d  %s:%d ", arg_p->instance_id, arg_p->hostname, arg_p->io_port);
            }
            uma_dbg_set_name(name);
        }
        break;
    }

    //#warning Start of specific application initialization code


    return ret;
}

/**
 *  Init of the data structure used for the non blocking entity

  @retval 0 on success
  @retval -1 on error
 */
int storaged_non_blocking_init(storaged_start_conf_param_t *args_p) {
    int ret;
    //  sem_t semForEver;    /* semaphore for blocking the main thread doing nothing */

    if (args_p->instance_id == 0) {
        //    info("FDL storaged_non_blocking_init instance 0  port %d ",args_p->debug_port);
    }
    ret = ruc_init(FALSE, args_p);

    if (ret != RUC_OK) return -1;


    return 0;

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
int storaged_start_nb_blocking_th(void *args) {
    int ret;
    //sem_t semForEver;    /* semaphore for blocking the main thread doing nothing */
    storaged_start_conf_param_t *args_p = (storaged_start_conf_param_t*) args;

    ret = storaged_non_blocking_init(args_p);
    if (ret != RUC_OK) {
        /*
         ** fatal error
         */
        fdl_debug_loop(__LINE__);
        fatal("can't initialize non blocking thread");
        return -1;
    }
    /*
     ** add profiler subject 
     */
    if (args_p->instance_id == 0) {
        uma_dbg_addTopic("profiler", show_profile_storaged_master_display);
    } else {
        uma_dbg_addTopic("profiler", show_profile_storaged_io_display);
    }
    uma_dbg_addTopic("uptime", show_uptime);

    info("storaged non-blocking thread started "
            "(instance: %d, host: %s, port: %d).",
            args_p->instance_id, args_p->hostname, args_p->debug_port);

    /*
     ** main loop
     */
    while (1) {
        ruc_sockCtrl_selectWait();
    }
    fatal("Exit from ruc_sockCtrl_selectWait()");
    fdl_debug_loop(__LINE__);
}
