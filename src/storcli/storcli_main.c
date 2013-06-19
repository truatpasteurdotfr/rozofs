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
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/stat.h>
#include <netinet/tcp.h>
#include <sys/resource.h>
#include <unistd.h>
#include <libintl.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>
#include <getopt.h>
#include <time.h>

#include <rozofs/rozofs_srv.h>
#include <rozofs/rozofs_timer_conf.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/mproto.h>
#include <rozofs/rpc/sproto.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/rpc/mclient.h>
#include <rozofs/rpc/eclient.h>
#include <rozofs/rpc/stcpproto.h>
#include <rozofs/rpc/storcli_lbg_prototypes.h>
#include <rozofs/core/north_lbg_api.h>
#include <rozofs/core/rozofs_timer_conf_dbg.h>

#include "rozofs_storcli_lbg_cnf_supervision.h"
#include "rozofs_storcli.h"
#include "storcli_main.h"


#define STORCLI_PID_FILE "storcli.pid"

int rozofs_storcli_non_blocking_init(uint16_t dbg_port, uint16_t rozofsmount_instance);

DEFINE_PROFILING(stcpp_profiler_t) = {0};

/**
 * data structure used to store the configuration parameter of a storcli process
 */
typedef struct storcli_conf {
    char *host; /**< hostname of the export from which the storcli will get the mstorage configuration  */
    char *export; /**< pathname of the exportd (unique) */
    char *passwd; /**< user password */
    char *mount; /**< mount point */
    int module_index; /**< storcli instance number within the exportd: more that one storcli processes can be started */
    unsigned buf_size;
    unsigned max_retry;
    unsigned dbg_port;
    unsigned rozofsmount_instance;
} storcli_conf;

/*
** KPI for Mojette transform
*/
 storcli_kpi_t storcli_kpi_transform_forward;
 storcli_kpi_t storcli_kpi_transform_inverse;

char storcli_process_filename[NAME_MAX];

static char localBuf[4096];
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

#define RESET_PROFILER_PROBE(probe) \
{ \
         gprofiler.probe[P_COUNT] = 0;\
         gprofiler.probe[P_ELAPSE] = 0; \
}

#define RESET_PROFILER_PROBE_BYTE(probe) \
{ \
   RESET_PROFILER_PROBE(probe);\
   gprofiler.probe[P_BYTES] = 0; \
}


#define SHOW_PROFILER_PROBE(probe) pChar += sprintf(pChar," %-14s | %15"PRIu64"  | %9"PRIu64"  | %18"PRIu64"  | %15s |\n",\
					#probe,\
					gprofiler.probe[P_COUNT],\
					gprofiler.probe[P_COUNT]?gprofiler.probe[P_ELAPSE]/gprofiler.probe[P_COUNT]:0,\
					gprofiler.probe[P_ELAPSE]," ");

#define SHOW_PROFILER_PROBE_BYTE(probe) pChar += sprintf(pChar," %-14s | %15"PRIu64"  | %9"PRIu64"  | %18"PRIu64"  | %15"PRIu64" |\n",\
					#probe,\
					gprofiler.probe[P_COUNT],\
					gprofiler.probe[P_COUNT]?gprofiler.probe[P_ELAPSE]/gprofiler.probe[P_COUNT]:0,\
					gprofiler.probe[P_ELAPSE],\
                    gprofiler.probe[P_BYTES]);


#define SHOW_PROFILER_KPI_BYTE(probe,kpi_buf) pChar += sprintf(pChar," %-14s | %15"PRIu64"  | %9"PRIu64"  | %18"PRIu64"  | %15"PRIu64" |\n",\
					#probe,\
					kpi_buf.count,\
					kpi_buf.count?kpi_buf.elapsed_time/kpi_buf.count:0,\
					kpi_buf.elapsed_time,\
                    kpi_buf.bytes_count);
                    

#define RESET_PROFILER_KPI_BYTE(probe,kpi_buf) \
{ \
					kpi_buf.count = 0; \
					kpi_buf.elapsed_time = 0;\
                    kpi_buf.bytes_count = 0; \
}

void show_profiler(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = localBuf;
    time_t elapse;
    int days, hours, mins, secs;
    int reset = 0;
    
    if (argv[1] != NULL)
    {
      if (strcmp(argv[1],"reset")==0) reset = 1;
    }
    if (reset)
    {
      RESET_PROFILER_PROBE_BYTE(read);
      RESET_PROFILER_KPI_BYTE(Mojette Inv,storcli_kpi_transform_inverse);
      RESET_PROFILER_PROBE_BYTE(read_prj);
      RESET_PROFILER_PROBE(read_prj_err);
      RESET_PROFILER_PROBE(read_prj_tmo);
      RESET_PROFILER_PROBE_BYTE(write)
      RESET_PROFILER_KPI_BYTE(Mojette Fwd,storcli_kpi_transform_forward);;
      RESET_PROFILER_PROBE_BYTE(write_prj);
      RESET_PROFILER_PROBE(write_prj_tmo);
      RESET_PROFILER_PROBE(write_prj_err);    
      uma_dbg_send(tcpRef, bufRef, TRUE, "Reset Done\n");    
      return;
      
    }

    // Compute uptime for storaged process
    elapse = (int) (time(0) - gprofiler.uptime);
    days = (int) (elapse / 86400);
    hours = (int) ((elapse / 3600) - (days * 24));
    mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
    secs = (int) (elapse % 60);


    pChar += sprintf(pChar, "GPROFILER version %s uptime =  %d days, %d:%d:%d\n", gprofiler.vers,days, hours, mins, secs);
    pChar += sprintf(pChar, "   procedure    |     count        |  time(us)  | cumulated time(us)  |     bytes       |\n");
    pChar += sprintf(pChar, "----------------+------------------+------------+---------------------+-----------------+\n");

//    SHOW_PROFILER_PROBE_BYTE(read_req);
    SHOW_PROFILER_PROBE_BYTE(read);
    SHOW_PROFILER_KPI_BYTE(Mojette Inv,storcli_kpi_transform_inverse);
    SHOW_PROFILER_PROBE_BYTE(read_prj);
    SHOW_PROFILER_PROBE(read_prj_err);
    SHOW_PROFILER_PROBE(read_prj_tmo);
    SHOW_PROFILER_PROBE_BYTE(write)
    SHOW_PROFILER_KPI_BYTE(Mojette Fwd,storcli_kpi_transform_forward);;
    SHOW_PROFILER_PROBE_BYTE(write_prj);
    SHOW_PROFILER_PROBE(write_prj_tmo);
    SHOW_PROFILER_PROBE(write_prj_err);

    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}

/*__________________________________________________________________________
 */
/**
 *  Global and local datas
 */
static storcli_conf conf;
exportclt_t exportclt; /**< structure associated to exportd, needed for communication */
uint32_t *rozofs_storcli_cid_table[ROZOFS_CLUSTERS_MAX];

storcli_lbg_cnx_supervision_t storcli_lbg_cnx_supervision_tab[STORCLI_MAX_LBG];


/*__________________________________________________________________________
 */


/**________________________________________________________________________
*/
/**
*  Display of the state of the current configuration of the exportd

 */

static char *show_storcli_display_poll_state(char *buffer,int state)
{
    char *pchar = buffer;
   switch (state)
   {
      default:
      case STORCLI_POLL_IDLE:
        sprintf(pchar,"IDLE  ");
        break;
   
      case STORCLI_POLL_IN_PRG:
        sprintf(pchar,"IN_PRG");
        break;   
      case STORCLI_POLL_ERR:
        sprintf(pchar,"ERROR ");
        break;   
   
   }
   return buffer;
}
/*
**________________________________________________________________________
*/
static char bufall[1024];
char *display_mstorage(mstorage_t *s,char *buffer)
{

  int i;

  for (i = 0; i< s->sids_nb; i++)
  {
     buffer += sprintf(buffer," %3.3d  |  %2.2d  |",s->cids[i],s->sids[i]);
     buffer += sprintf(buffer," %-20s |",s->host);
     if ( s->lbg_id == -1)
     {
       buffer += sprintf(buffer,"  ???     |");
     }
     else
     {
       buffer += sprintf(buffer,"  %3d     |",s->lbg_id);       
     }
     buffer += sprintf(buffer,"  %s  |",north_lbg_display_lbg_state(bufall,s->lbg_id));         
     buffer += sprintf(buffer,"  %s      |",north_lbg_is_available(s->lbg_id)==1 ?"UP  ":"DOWN");        
     buffer += sprintf(buffer," %3s |",storcli_lbg_cnx_supervision_tab[s->lbg_id].state==STORCLI_LBG_RUNNING ?"YES":"NO");        
     buffer += sprintf(buffer," %5d |",storcli_lbg_cnx_supervision_tab[s->lbg_id].tmo_counter); 
     buffer += sprintf(buffer," %5d |",storcli_lbg_cnx_supervision_tab[s->lbg_id].poll_counter); 
     buffer += sprintf(buffer," %2d |",STORCLI_LBG_SP_NULL_INTERVAL);         
     buffer += sprintf(buffer,"  %s      |\n",show_storcli_display_poll_state(bufall,storcli_lbg_cnx_supervision_tab[s->lbg_id].poll_state));         
  }
  return buffer;
}



/**
*  Display the configuration et operationbal status of the storaged


*/
void show_storage_configuration(char * argv[], uint32_t tcpRef, void *bufRef) 
{
    char *pchar = localBuf;

   pchar +=sprintf(pchar," cid  |  sid |      hostname        |  lbg_id  | state  | Path state | Sel | tmo   | Poll. |Per.|  poll state  |\n");
   pchar +=sprintf(pchar,"------+------+----------------------+----------+--------+------------+-----+-------+-------+----+--------------+\n");

   list_t *iterator = NULL;
   /* Search if the node has already been created  */
   list_for_each_forward(iterator, &exportclt.storages) 
   {
     mstorage_t *s = list_entry(iterator, mstorage_t, list);

     /*
     ** entry is found 
     ** update the cid and sid part only. changing the number of
     ** ports of the mstorage is not yet supported.
     */
     pchar=display_mstorage(s,pchar);
   }
   uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);      
}      

/*__________________________________________________________________________
 */

/**
 * init of the cid table. That table contains pointers to
 * a sid/lbg_id association table, the primary key being the sid index
 */

void rozofs_storcli_cid_table_init() {
    memset(rozofs_storcli_cid_table, 0, ROZOFS_CLUSTERS_MAX * sizeof (uint32_t *));
}

/*__________________________________________________________________________
 */

/**
 *  insert an entry in the  rozofs_storcli_cid_table table

   @param cid : cluster index
   @param sid : storage index
   @param lbg_id : load balancing group index
   
   @retval 0 on success
   @retval < 0 on error
 */
int rozofs_storcli_cid_table_insert(cid_t cid, sid_t sid, uint32_t lbg_id) {
    uint32_t *sid_lbg_id_p;

    if (cid >= ROZOFS_CLUSTERS_MAX) {
        /*
         ** out of range
         */
        return -1;
    }

    sid_lbg_id_p = rozofs_storcli_cid_table[cid - 1];
    if (sid_lbg_id_p == NULL) {
        /*
         ** allocate the cid/lbg_id association table if it does not
         ** exist
         */
        sid_lbg_id_p = xmalloc(sizeof (uint32_t)*(SID_MAX + 1));
        memset(sid_lbg_id_p, -1, sizeof (uint32_t)*(SID_MAX + 1));
        rozofs_storcli_cid_table[cid - 1] = sid_lbg_id_p;
    }
    sid_lbg_id_p[sid - 1] = lbg_id;
    /*
    ** clear the tmo supervision structure assocated with the lbg
    */
    storcli_lbg_cnx_sup_clear_tmo(lbg_id);
    
    return 0;

}

/*__________________________________________________________________________
 */

/** Send a request to a storage node for get the list of TCP ports this storage
 *
 * @param storage: the storage node
 *
 * @return 0 on success otherwise -1
 */
static int get_storage_ports(mstorage_t *s) {
    int status = -1;
    int i = 0;
    mclient_t mclt;

    uint32_t ports[STORAGE_NODE_PORTS_MAX];
    memset(ports, 0, sizeof (uint32_t) * STORAGE_NODE_PORTS_MAX);
    strncpy(mclt.host, s->host, ROZOFS_HOSTNAME_MAX);

    struct timeval timeo;
    timeo.tv_sec = ROZOFS_MPROTO_TIMEOUT_SEC;
    timeo.tv_usec = 0;

    /* Initialize connection with storage (by mproto) */
    if (mclient_initialize(&mclt, timeo) != 0) {
        severe("Warning: failed to join storage (host: %s), %s.\n",
                s->host, strerror(errno));
        goto out;
    } else {
        /* Send request to get storage TCP ports */
        if (mclient_ports(&mclt, ports) != 0) {
            severe("Warning: failed to get ports for storage (host: %s).\n",
                    s->host);
            goto out;
        }
    }

    /* Copy each TCP ports */
    for (i = 0; i < STORAGE_NODE_PORTS_MAX; i++) {
        if (ports[i] != 0) {
            strncpy(s->sclients[i].host, s->host, ROZOFS_HOSTNAME_MAX);
            s->sclients[i].port = ports[i];
            s->sclients[i].status = 0;
            s->sclients_nb++;
        }
    }

    /* Release mclient*/
    mclient_release(&mclt);

    status = 0;
out:
    return status;
}

/*__________________________________________________________________________
 */
/** Thread : Check if the connections for one storage node are active or not
 *
 * @param storage: the storage node
 */
#define CONNECTION_THREAD_TIMESPEC  2

void *connect_storage(void *v) {
    mstorage_t *mstorage = (mstorage_t*) v;
    int configuration_done = 0;

    struct timespec ts = {CONNECTION_THREAD_TIMESPEC, 0};

    if (mstorage->sclients_nb != 0) {
        configuration_done = 1;
        ts.tv_sec = CONNECTION_THREAD_TIMESPEC * 20;
    }

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

    for (;;) {
        if (configuration_done == 0) {
            /* We don't have the ports for this storage node */
            if (mstorage->sclients_nb == 0) {
                /* Get ports for this storage node */
                if (get_storage_ports(mstorage) != 0) {
                    DEBUG("Cannot get ports for host: %s", mstorage->host);
                }
            }
            if (mstorage->sclients_nb != 0) {
                /*
                 ** configure the load balancing group is not yet done:
                 ** here we have to address the race competition case since
                 ** that thread runs in parallel with the socket controller, so
                 ** we cannot configure the load balancing group from that thread
                 ** we just can assert a flag to indicate that the configuration
                 ** data of the lbg are available.S
                 */
                storcli_sup_send_lbg_port_configuration(STORCLI_LBG_ADD, (void *) mstorage);
                configuration_done = 1;

                ts.tv_sec = CONNECTION_THREAD_TIMESPEC * 20;

            }
        }

        nanosleep(&ts, NULL);
    }
    return 0;
}
/*__________________________________________________________________________
 */

/**
 *  API to start the connect_storage thread
 *
 *   The goal of that thread is to retrieve the port configuration for the mstorage
   that were not accessible at the time storcli has started.
   That function is intended to be called for the following cases:
     - after the retrieving of the storage configuration from the exportd
     - after the reload of the configuration from the exportd
    
 @param none
 
 @retval none
 */
void rozofs_storcli_start_connect_storage_thread() {
    list_t *p = NULL;

    list_for_each_forward(p, &exportclt.storages) {

        mstorage_t *storage = list_entry(p, mstorage_t, list);
        if (storage->thread_started == 1) {
            /*
             ** thread has already been started
             */
            continue;
        }
        pthread_t thread;

        if ((errno = pthread_create(&thread, NULL, connect_storage, storage)) != 0) {
            severe("can't create connexion thread: %s", strerror(errno));
        }
        storage->thread_started = 1;
    }
}
/*__________________________________________________________________________
 */

/**
 *  Get the exportd configuration:
  The goal of that procedure is to get the list of the mstorages
  that are associated with the exportd that is referenced as input
  argument of the storage client process
  
  that API uses the parameters stored in the conf structure

  @param none
  
  @retval 0 on success
  @retval -1 on error
 */
int rozofs_storcli_get_export_config(storcli_conf *conf) {

    int i = 0;
    int ret;
    list_t *iterator = NULL;

    /* Initialize rozofs */
    rozofs_layout_initialize();

    struct timeval timeout_exportd;

    //XXX Static timeout
    timeout_exportd.tv_sec = ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM);
    timeout_exportd.tv_usec = 0;

    /* Initiate the connection to the export and get informations
     * about exported filesystem */
    if (exportclt_initialize(
            &exportclt,
            conf->host,
            conf->export,
            conf->passwd,
            conf->buf_size * 1024,
            conf->max_retry,
            timeout_exportd) != 0) {
        fprintf(stderr,
                "storcli failed for:\n" "export directory: %s\n"
                "export hostname: %s\n" "error: %s\n"
                "See log for more information\n", conf->export, conf->host,
                strerror(errno));
        return -1;
    }

    /* Initiate the connection to each storage node (with mproto),
     *  get the list of ports and
     *  establish a connection with each storage socket (with sproto) */
    list_for_each_forward(iterator, &exportclt.storages) {

        mstorage_t *s = list_entry(iterator, mstorage_t, list);

        mclient_t mclt;
        strcpy(mclt.host, s->host);
        uint32_t ports[STORAGE_NODE_PORTS_MAX];
        memset(ports, 0, sizeof (uint32_t) * STORAGE_NODE_PORTS_MAX);
        /*
         ** allocate the load balancing group for the mstorage
         */
        s->lbg_id = north_lbg_create_no_conf();
        if (s->lbg_id < 0) {
            severe(" out of lbg contexts");
            goto fatal;
        }

        struct timeval timeout_mproto;
        timeout_mproto.tv_sec = ROZOFS_TMR_GET(TMR_MONITOR_PROGRAM);
        timeout_mproto.tv_usec = 0;

        /* Initialize connection with storage (by mproto) */
        if (mclient_initialize(&mclt, timeout_mproto) != 0) {
            fprintf(stderr, "Warning: failed to join storage (host: %s), %s.\n",
                    s->host, strerror(errno));
        } else {
            /* Send request to get storage TCP ports */
            if (mclient_ports(&mclt, ports) != 0) {
                fprintf(stderr,
                        "Warning: failed to get ports for storage (host: %s).\n"
                        , s->host);
            }
        }

        /* Initialize each TCP ports connection with this storage node
         *  (by sproto) */
        for (i = 0; i < STORAGE_NODE_PORTS_MAX; i++) {
            if (ports[i] != 0) {
                strcpy(s->sclients[i].host, s->host);
                s->sclients[i].port = ports[i];
                s->sclients[i].status = 0;
                s->sclients_nb++;
            }
        }
        /*
         ** proceed with storage configuration if the number of port is different from 0
         */
        if (s->sclients_nb != 0) {
            ret = storaged_lbg_initialize(s);
            if (ret < 0) {
                goto fatal;
            }
        }
        /*
         ** init of the cid/sid<-->lbg_id association table
         */
        for (i = 0; i < s->sids_nb; i++) {
            rozofs_storcli_cid_table_insert(s->cids[i], s->sids[i], s->lbg_id);
        }
        /* Release mclient*/
        mclient_release(&mclt);
    }
    return 0;


fatal:
    return -1;
}







void usage() {
    printf("Rozofs storage client daemon - %s\n", VERSION);
    printf("Usage: storcli -i <instance> [OPTIONS]\n\n");
    printf("\t-h, --help\tprint this message.\n");
    printf("\t-H,--host EXPORT_HOST\t\tdefine address (or dns name) where exportd deamon is running (default: rozofsexport) \n");
    printf("\t-E,--path EXPORT_PATH\t\tdefine path of an export see exportd (default: /srv/rozofs/exports/export)\n");
    printf("\t-M,--mount MOUNT_POINT\t\tmount point\n");
    printf("\t-P,--pwd EXPORT_PASSWD\t\tdefine passwd used for an export see exportd (default: none) \n");
    printf("\t-D,--dbg DEBUG_PORT\t\tdebug port (default: none) \n");
    printf("\t-R,--rozo_instance ROZO_INSTANCE\t\trozofsmount instance number \n");
    printf("\t-i,--instance index\t\t unique index of the module instance related to export \n");
    printf("\t-s,--storagetmr \t\t define timeout (s) for IO storaged requests (default: 3)\n");
}

/**
*  Signal catching
*/

static void storlci_handle_signal(int sig)
{
   unlink(storcli_process_filename); 
   signal(sig, SIG_DFL);
   raise(sig);
}



int main(int argc, char *argv[]) {
    int c;
    int ret;
    int val;
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "host", required_argument, 0, 'H'},
        { "path", required_argument, 0, 'E'},
        { "pwd", required_argument, 0, 'P'},
        { "dbg", required_argument, 0, 'D'},
        { "mount", required_argument, 0, 'M'},
        { "instance", required_argument, 0, 'i'},
        { "rozo_instance", required_argument, 0, 'R'},
        { "storagetmr", required_argument, 0, 's'},
        { 0, 0, 0, 0}
    };
    /*
    ** init of the timer configuration
    */
    rozofs_tmr_init_configuration();
    
    storcli_process_filename[0] = 0;


    signal(SIGCHLD, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGILL, storlci_handle_signal);
    signal(SIGSTOP, storlci_handle_signal);
    signal(SIGABRT, storlci_handle_signal);
    signal(SIGSEGV, storlci_handle_signal);
    signal(SIGKILL, storlci_handle_signal);
    signal(SIGTERM, storlci_handle_signal);
    signal(SIGQUIT, storlci_handle_signal);
    

    conf.host = NULL;
    conf.passwd = NULL;
    conf.export = NULL;
    conf.mount = NULL;
    conf.module_index = -1;
    conf.buf_size = 256;
    conf.max_retry = 3;
    conf.dbg_port = 0;
    conf.rozofsmount_instance = 0;

    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hH:E:P:i:D:M:R:s:", long_options, &option_index);

        if (c == -1)
            break;

        switch (c) {
            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 'H':
                conf.host = strdup(optarg);
                break;
            case 'E':
                conf.export = strdup(optarg);
                break;
            case 'P':
                conf.passwd = strdup(optarg);
                break;
            case 'M':
                conf.mount = strdup(optarg);
                break;
            case 'i':
                errno = 0;
                val = (int) strtol(optarg, (char **) NULL, 10);
                if (errno != 0) {
                    strerror(errno);
                    usage();
                    exit(EXIT_FAILURE);
                }
                conf.module_index = val;
                break;

            case 'D':
                errno = 0;
                val = (int) strtol(optarg, (char **) NULL, 10);
                if (errno != 0) {
                    strerror(errno);
                    usage();
                    exit(EXIT_FAILURE);
                }
                conf.dbg_port = val;
                break;
            case 'R':
                errno = 0;
                val = (int) strtol(optarg, (char **) NULL, 10);
                if (errno != 0) {
                    strerror(errno);
                    usage();
                    exit(EXIT_FAILURE);
                }
                conf.rozofsmount_instance = val;
                break;
            case 's':
                errno = 0;
                val = (int) strtol(optarg, (char **) NULL, 10);
                if (errno != 0) {
                    strerror(errno);
                    usage();
                    exit(EXIT_FAILURE);
                }
                rozofs_tmr_configure(TMR_STORAGE_PROGRAM,val);
                break;
            case '?':
                usage();
                exit(EXIT_SUCCESS);
                break;
            default:
                usage();
                exit(EXIT_FAILURE);
                break;
        }
    }
    /*
     ** Check the parameters
     */
    if (conf.module_index == -1) {
        printf("instance number is mandatory!\n");
        usage();
        exit(EXIT_FAILURE);
    }

    if (conf.host == NULL) {
        conf.host = strdup("rozofsexport");
    }

    if (conf.export == NULL) {
        conf.export = strdup("/srv/rozofs/exports/export");
    }

    if (conf.passwd == NULL) {
        conf.passwd = strdup("none");
    }
    openlog("storcli", LOG_PID, LOG_DAEMON);
    
    rozofs_storcli_cid_table_init();
    storcli_lbg_cnx_sup_init();

    gprofiler.uptime = time(0);
    /*
    ** clear KPI counters
    */
    memset(&storcli_kpi_transform_forward,0,sizeof(  storcli_kpi_transform_forward));
    memset(&storcli_kpi_transform_inverse,0,sizeof(  storcli_kpi_transform_inverse));

    /*
    ** create the process filename
    */
    int ppfd;
    sprintf(storcli_process_filename, "%s%s_%d_storcli_%d", DAEMON_PID_DIRECTORY, "rozofsmount",conf.rozofsmount_instance, conf.module_index);

    if ((ppfd = open(storcli_process_filename, O_RDWR | O_CREAT, 0640)) < 0) {
        severe("can't open process file");
    } else {
        char str[10];
        sprintf(str, "%d\n", getpid());
        write(ppfd, str, strlen(str));
        close(ppfd);
    }    
    
    /*
     ** init of the non blocking part
     */
    ret = rozofs_storcli_non_blocking_init(conf.dbg_port, conf.rozofsmount_instance);
    if (ret < 0) {
        fprintf(stderr, "Fatal error while initializing non blocking entity\n");
        goto error;
    }
    {
        char name[32];
        sprintf(name, "storcli %d of rozofsmount %d", conf.module_index, conf.rozofsmount_instance);
        uma_dbg_set_name(name);
    }

    /*
     ** Get the configuration from the export
     */
    ret = rozofs_storcli_get_export_config(&conf);
    if (ret < 0) {
        fprintf(stderr, "Fatal error on rozofs_storcli_get_export_config()\n");
        goto error;
    }
    /*
    ** Declare the debug entry to get the currrent configuration of the storcli
    */
    uma_dbg_addTopic("storaged_status", show_storage_configuration);
    /*
     ** Init of the north interface (read/write request processing)
     */
    //#warning buffer size and count must not be hardcoded
    ret = rozofs_storcli_north_interface_init(
            exportclt.eid, conf.rozofsmount_instance, conf.module_index,
            STORCLI_NORTH_LBG_BUF_RECV_CNT, STORCLI_NORTH_LBG_BUF_RECV_SZ);
    if (ret < 0) {
        fprintf(stderr, "Fatal error on rozofs_storcli_get_export_config()\n");
        goto error;
    }
    rozofs_storcli_start_connect_storage_thread();
    /*
     ** add the topic for the local profiler
     */
    uma_dbg_addTopic("profiler", show_profiler);
    uma_dbg_addTopic("uptime", show_uptime);
    /*
    ** declare timer debug functions
    */
    rozofs_timer_conf_dbg_init();
    
    /*
     ** main loop
     */
    info("storcli started (instance: %d, rozofs instance: %d, mountpoint: %s).",
            conf.module_index, conf.rozofsmount_instance, conf.mount);

    while (1) {
        ruc_sockCtrl_selectWait();
    }

error:
    fprintf(stderr, "see log for more details.\n");
    exit(EXIT_FAILURE);
}
