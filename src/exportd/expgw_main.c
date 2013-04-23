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
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/list.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/eproto.h>
#include <rozofs/rpc/epproto.h>
#include <rozofs/rpc/mclient.h>
#include <rozofs/rpc/eclient.h>
#include <rozofs/core/north_lbg_api.h>
#include "expgw_main.h"
#include "expgw_export.h"
#include <rozofs/core/expgw_common.h>


#define EXPGW_PID_FILE "expgw.pid"

int rozofs_expgw_non_blocking_init(uint16_t listening_port_base, uint16_t rozofsmount_instance);
uint32_t expgw_local_ipaddr = INADDR_ANY;

DEFINE_PROFILING(epp_profiler_t) = {0};

/**
 * data structure used to store the configuration parameter of a expgw process
 */
typedef struct expgw_conf {
    char *host; /**< hostname of the export from which the expgw will get the mstorage configuration  */
    char *localhost; /**< local host name  */
    char *export; /**< pathname of the exportd (unique) */
    char *passwd; /**< user password */
    char *mount; /**< mount point */
    int module_index; /**< expgw instance number within the exportd: more that one expgw processes can be started */
    unsigned buf_size;
    unsigned max_retry;
    unsigned listening_port_base;
    unsigned rozofsmount_instance;
} expgw_conf;

static char localBuf[8192];


#define SHOW_PROFILER_PROBE(probe) pChar += sprintf(pChar," %14s | %15"PRIu64"  | %9"PRIu64"  | %18"PRIu64"  |\n",\
					#probe,\
					gprofiler.probe[P_COUNT],\
					gprofiler.probe[P_COUNT]?gprofiler.probe[P_ELAPSE]/gprofiler.probe[P_COUNT]:0,\
					gprofiler.probe[P_ELAPSE]);

#define SHOW_PROFILER_PROBE_BYTE(probe) pChar += sprintf(pChar," %14s | %15"PRIu64"  | %9"PRIu64"  | %18"PRIu64"  | %15"PRIu64" \n",\
					#probe,\
					gprofiler.probe[P_COUNT],\
					gprofiler.probe[P_COUNT]?gprofiler.probe[P_ELAPSE]/gprofiler.probe[P_COUNT]:0,\
					gprofiler.probe[P_ELAPSE],\
                    gprofiler.probe[P_BYTES]);

void show_profiler(char * argv[], uint32_t tcpRef, void *bufRef) {

    char *pChar = localBuf;

    pChar += sprintf(pChar, "GPROFILER version %s uptime = %llu\n", gprofiler.vers, (long long unsigned int) gprofiler.uptime);
    pChar += sprintf(pChar, "   procedure    |     count       |  time(us) | cumulated time(us) |     bytes       \n");
    pChar += sprintf(pChar, "----------------+-----------------+-----------+--------------------+-----------------\n");
#if 1
    SHOW_PROFILER_PROBE(ep_lookup);
    SHOW_PROFILER_PROBE(ep_getattr);
    SHOW_PROFILER_PROBE(ep_setattr);
    SHOW_PROFILER_PROBE(ep_readlink);
    SHOW_PROFILER_PROBE(ep_mknod);
    SHOW_PROFILER_PROBE(ep_unlink);
    SHOW_PROFILER_PROBE(ep_mkdir);
    SHOW_PROFILER_PROBE(ep_rmdir);
    SHOW_PROFILER_PROBE(ep_symlink);
    SHOW_PROFILER_PROBE(ep_rename);
    SHOW_PROFILER_PROBE(ep_readdir);
    SHOW_PROFILER_PROBE(ep_read_block);
    SHOW_PROFILER_PROBE(ep_write_block);
    SHOW_PROFILER_PROBE(ep_link);
    SHOW_PROFILER_PROBE(ep_setxattr);
    SHOW_PROFILER_PROBE(ep_getxattr);
    SHOW_PROFILER_PROBE(ep_removexattr);
    SHOW_PROFILER_PROBE(ep_listxattr);
#endif
    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}



void show_fid_cache(char * argv[], uint32_t tcpRef, void *bufRef) {

    char *pChar = localBuf;
    
    com_cache_show_cache_stats(pChar,expgw_fid_cache_p,"FID CACHE");

    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}


void show_attr_cache(char * argv[], uint32_t tcpRef, void *bufRef) {

    char *pChar = localBuf;
    
    com_cache_show_cache_stats(pChar,expgw_attr_cache_p,"ATTRIBUTES CACHE");

    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}




void show_exp_routing_table(char * argv[], uint32_t tcpRef, void *bufRef) {

    char *pChar = localBuf;
    
    expgw_display_all_exportd_routing_table(pChar);

    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}


void show_eid_exportd_assoc(char * argv[], uint32_t tcpRef, void *bufRef) {

    char *pChar = localBuf;
    
    expgw_display_all_eid(pChar);

    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}


/*__________________________________________________________________________
 */
/**
 *  Global and local datas
 */
static expgw_conf conf;
uint32_t *rozofs_expgw_cid_table[ROZOFS_CLUSTERS_MAX];








void usage() {
    printf("Rozofs storage client daemon - %s\n", VERSION);
    printf("Usage: expgw -i <instance> [OPTIONS]\n\n");
    printf("\t-h, --help\tprint this message.\n");
    printf("\t-H,--host EXPORT_HOST\t\tdefine address (or dns name) where exportd deamon is running (default: rozofsexport) \n");
    printf("\t-P,--port LISTENING_PORT_BASE\t\trozofsmount,exportd,debug port (default: none) \n");
    printf("\t-L,--local LOCAL_HOST\t\tdefine address (or dns name) of the local host \n");
    printf("\t-E,--path EXPORT_PATH\t\tdefine path of an export see exportd (default: /srv/rozofs/exports/export)\n");
    printf("\t-M,--mount MOUNT_POINT\t\tmount point\n");
    printf("\t-R,--rozo_instance ROZO_INSTANCE\t\trozofsmount instance number \n");
    printf("\t-i,--instance index\t\t unique index of the module instance related to export \n");
}

int main(int argc, char *argv[]) {
    int c;
    int ret;
    int val;
    static struct option long_options[] = {
        { "help", no_argument, 0, 'h'},
        { "host", required_argument, 0, 'H'},
        { "local", required_argument, 0, 'L'},
        { "path", required_argument, 0, 'E'},
        { "port", required_argument, 0, 'P'},
        { "mount", required_argument, 0, 'M'},
        { "instance", required_argument, 0, 'i'},
        { "rozo_instance", required_argument, 0, 'R'},
        { 0, 0, 0, 0}
    };

    conf.host = NULL;
    conf.localhost = NULL;
    conf.passwd = NULL;
    conf.export = NULL;
    conf.mount = NULL;
    conf.module_index = -1;
    conf.buf_size = 256;
    conf.max_retry = 3;
    conf.listening_port_base = 0;
    conf.rozofsmount_instance = 0;

    while (1) {

        int option_index = 0;
        c = getopt_long(argc, argv, "hH:E:i:P:M:R:L:", long_options, &option_index);

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
            case 'L':
                conf.localhost = strdup(optarg);
                break;
            case 'E':
                conf.export = strdup(optarg);
                break;

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

            case 'P':
                errno = 0;
                val = (int) strtol(optarg, (char **) NULL, 10);
                if (errno != 0) {
                    strerror(errno);
                    usage();
                    exit(EXIT_FAILURE);
                }
                conf.listening_port_base = val;
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
    openlog("expgw", LOG_PID, LOG_DAEMON);

    /*
     ** init of the non blocking part
     */
    ret = expgw_non_blocking_init(conf.listening_port_base+(100*conf.module_index)+EXPGW_PORT_DEBUG_IDX, conf.rozofsmount_instance);
    if (ret < 0) {
        fprintf(stderr, "Fatal error while initializing non blocking entity\n");
        goto error;
    }
    {
        char name[32];
        sprintf(name, "expgw %d instance %d", conf.module_index, conf.rozofsmount_instance);
        uma_dbg_set_name(name);
    }

    /*
    ** get the IP address of the local host
    */
    if (conf.localhost != NULL)
    {
       if (expgw_host2ip(conf.localhost,&expgw_local_ipaddr) < 0)
       {
        fprintf(stderr, "Cannot get local IP address: %s\n",conf.localhost);
        goto error;       
       
       }
    }
    /*
     ** Init of the north interface (read/write request processing)
     */
    ret = expgw_rozofs_north_interface_init(
            expgw_local_ipaddr, conf.listening_port_base+EXPGW_PORT_ROZOFSMOUNT_IDX,
            EXPGW_NORTH_LBG_BUF_RECV_CNT, EXPGW_NORTH_LBG_BUF_RECV_SZ);
    if (ret < 0) {
        fprintf(stderr, "Fatal error on expgw_rozofs_north_interface_init()\n");
        goto error;
    }
    /*
    ** init of fid and attribute cache
    */
    ret = expgw_fid_cache_init();
    if (ret < 0) {
        fprintf(stderr, "Fatal error on expgw_fid_cache_init()\n");
        goto error;
    }

    ret = expgw_attr_cache_init();
    if (ret < 0) {
        fprintf(stderr, "Fatal error on expgw_fid_cache_init()\n");
        goto error;
    } 
    /*
    ** init of the exportd/eid routing table
    */
    expgw_export_tableInit();
    int idx = -1;
    sscanf(conf.localhost,"localhost%d",&idx);
    ret = expgw_export_add_eid(1,   // exportd id
                               1,   // eid
                               conf.host,  // hostname of the Master exportd
                               0,  // port
                               2,  // nb Gateway
                               idx-1   // gateway rank
                               );
    if (ret < 0) {
        fprintf(stderr, "Fatal error on expgw_export_add_eid()\n");
        goto error;
    } 
    if (strcmp(conf.localhost,"localhost1") == 0)
    {
      ret = expgw_add_export_gateway(1, "localhost2",conf.listening_port_base+EXPGW_PORT_ROZOFSMOUNT_IDX,1);  
    }
    else
    {
    
      ret = expgw_add_export_gateway(1, "localhost1",conf.listening_port_base+EXPGW_PORT_ROZOFSMOUNT_IDX,0);  
    
    }                     
    if (ret < 0) {
        fprintf(stderr, "Fatal error on expgw_add_export_gateway()\n");
        goto error;
    } 
 
    /*
     ** add the topic for the local profiler
     */
    uma_dbg_addTopic("profiler", show_profiler);
    /*
    ** add debug entry for fid and attributes caches
    */
    uma_dbg_addTopic("fid_cache", show_fid_cache);
    uma_dbg_addTopic("attr_cache", show_attr_cache);
    uma_dbg_addTopic("exp_route", show_exp_routing_table);
    uma_dbg_addTopic("exp_eid", show_eid_exportd_assoc);
    
    /*
     ** main loop
     */
    info("expgw %d of rozofsmount %d on mount point %s started", conf.module_index, conf.rozofsmount_instance, conf.mount);

    while (1) {
        ruc_sockCtrl_selectWait();
    }

error:
    fprintf(stderr, "see log for more details.\n");
    exit(EXIT_FAILURE);
}
