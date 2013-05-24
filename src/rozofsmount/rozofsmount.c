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
#include <errno.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <pthread.h>
#include <inttypes.h>
#include <assert.h>
#include <semaphore.h>
#include <netinet/tcp.h>

#define FUSE_USE_VERSION 26
#include <fuse/fuse_lowlevel.h>
#include <fuse/fuse_opt.h>

#include <rozofs/rozofs.h>
#include <rozofs/rozofs_debug_ports.h>
#include <rozofs/common/list.h>
#include <rozofs/common/log.h>
#include <rozofs/common/htable.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/rpc/storcli_lbg_prototypes.h>
#include <rozofs/rozofs_timer_conf.h>
#include <rozofs/core/rozofs_timer_conf_dbg.h>

#include "rozofs_fuse.h"
#include "rozofsmount.h"

#define hash_xor8(n)    (((n) ^ ((n)>>8) ^ ((n)>>16) ^ ((n)>>24)) & 0xff)
#define INODE_HSIZE 8192
#define PATH_HSIZE  8192

#define FUSE28_DEFAULT_OPTIONS "default_permissions,allow_other,fsname=rozofs,subtype=rozofs,big_writes"
#define FUSE27_DEFAULT_OPTIONS "default_permissions,allow_other,fsname=rozofs,subtype=rozofs"

#define CACHE_TIMEOUT 10.0

#define CONNECTION_THREAD_TIMESPEC  2

#define STORCLI_STARTER  "storcli_starter.sh"
#define STORCLI_KILLER  "storcli_killer.sh"
#define STORCLI_EXEC "storcli"

static SVCXPRT *rozofsmount_profile_svc = 0;

DEFINE_PROFILING(mpp_profiler_t) = {0};

char localBuf[4096];

sem_t *semForEver; /**< semaphore used for stopping rozofsmount: typically on umount */

/*
 *________________________________________________________
 */

/*
 ** API to be called for stopping rozofsmount

 @param none
 @retval none
 */
void rozofs_exit() {
    if (semForEver != NULL) {
        sem_post(semForEver);
        for (;;) {
            severe("RozofsMount exit required!!");
            sleep(10);
        }
        fatal("semForEver is not initialized !!");
    }
}

extern void rozofsmount_profile_program_1(struct svc_req *rqstp, SVCXPRT *ctl_svc);

static void usage(const char *progname) {
    fprintf(stderr, "Rozofs fuse mounter - %s\n", VERSION);
    fprintf(stderr, "Usage: %s mountpoint [options]\n", progname);
    fprintf(stderr, "general options:\n");
    fprintf(stderr, "\t-o opt,[opt...]\tmount options\n");
    fprintf(stderr, "\t-h --help\tprint help\n");
    fprintf(stderr, "\t-V --version\tprint rozofs version\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "ROZOFS options:\n");
    fprintf(stderr, "\t-H EXPORT_HOST\t\tdefine address (or dns name) where exportd deamon is running (default: rozofsexport) equivalent to '-o exporthost=EXPORT_HOST'\n");
    fprintf(stderr, "\t-E EXPORT_PATH\t\tdefine path of an export see exportd (default: /srv/rozofs/exports/export) equivalent to '-o exportpath=EXPORT_PATH'\n");
    fprintf(stderr, "\t-P EXPORT_PASSWD\t\tdefine passwd used for an export see exportd (default: none) equivalent to '-o exportpasswd=EXPORT_PASSWD'\n");
    fprintf(stderr, "\t-o rozofsbufsize=N\tdefine size of I/O buffer in KiB (default: 256)\n");
    fprintf(stderr, "\t-o rozofsmaxretry=N\tdefine number of retries before I/O error is returned (default: 50)\n");
    fprintf(stderr, "\t-o rozofsexporttimeout=N\tdefine timeout (s) for exportd requests (default: 25)\n");
    fprintf(stderr, "\t-o rozofsstoragetimeout=N\tdefine timeout (s) for IO storaged requests (default: 3)\n");
    fprintf(stderr, "\t-o rozofsstorclitimeout=N\tdefine timeout (s) for IO storcli requests (default: 10)\n");
    fprintf(stderr, "\t-o debug_port=N\tdefine the base debug port for Rozofsmount (default:none)\n");
    fprintf(stderr, "\t-o instance=N\tinstance number (default:0)\n");
}

static rozofsmnt_conf_t conf;

double direntry_cache_timeo = CACHE_TIMEOUT;
double entry_cache_timeo = CACHE_TIMEOUT;
double attr_cache_timeo = CACHE_TIMEOUT;

enum {
    KEY_EXPORT_HOST,
    KEY_EXPORT_PATH,
    KEY_EXPORT_PASSWD,
    KEY_HELP,
    KEY_VERSION,
    KEY_DEBUG_PORT,
};

#define MYFS_OPT(t, p, v) { t, offsetof(struct rozofsmnt_conf, p), v }

static struct fuse_opt rozofs_opts[] = {
    MYFS_OPT("exporthost=%s", host, 0),
    MYFS_OPT("exportpath=%s", export, 0),
    MYFS_OPT("exportpasswd=%s", passwd, 0),
    MYFS_OPT("rozofsbufsize=%u", buf_size, 0),
    MYFS_OPT("rozofsmaxretry=%u", max_retry, 0),
    MYFS_OPT("rozofsexporttimeout=%u", export_timeout, 0),
    MYFS_OPT("rozofsstoragetimeout=%u", storage_timeout, 0),
    MYFS_OPT("rozofsstorclitimeout=%u", storcli_timeout, 0),
    MYFS_OPT("debug_port=%u", dbg_port, 0),
    MYFS_OPT("instance=%u", instance, 0),

    FUSE_OPT_KEY("-H ", KEY_EXPORT_HOST),
    FUSE_OPT_KEY("-E ", KEY_EXPORT_PATH),
    FUSE_OPT_KEY("-P ", KEY_EXPORT_PASSWD),

    FUSE_OPT_KEY("-V", KEY_VERSION),
    FUSE_OPT_KEY("--version", KEY_VERSION),
    FUSE_OPT_KEY("-h", KEY_HELP),
    FUSE_OPT_KEY("--help", KEY_HELP),
    FUSE_OPT_END
};

static int myfs_opt_proc(void *data, const char *arg, int key,
        struct fuse_args *outargs) {
    (void) data;
    switch (key) {
        case FUSE_OPT_KEY_OPT:
            return 1;
        case FUSE_OPT_KEY_NONOPT:
            return 1;
        case KEY_EXPORT_HOST:
            if (conf.host == NULL) {
                conf.host = strdup(arg + 2);
            }
            return 0;
        case KEY_EXPORT_PATH:
            if (conf.export == NULL) {
                conf.export = strdup(arg + 2);
            }
            return 0;
        case KEY_EXPORT_PASSWD:
            if (conf.passwd == NULL) {
                conf.passwd = strdup(arg + 2);
            }
            return 0;
        case KEY_HELP:
            usage(outargs->argv[0]);
            fuse_opt_add_arg(outargs, "-h"); // PRINT FUSE HELP
            fuse_parse_cmdline(outargs, NULL, NULL, NULL);
            fuse_mount(NULL, outargs);
            exit(1);
        case KEY_VERSION:
            fprintf(stderr, "rozofs version %s\n", VERSION);
            fuse_opt_add_arg(outargs, "--version"); // PRINT FUSE VERSION
            fuse_parse_cmdline(outargs, NULL, NULL, NULL);
            exit(0);
    }
    return 1;
}

/**< structure associated to exportd, needed for communication */
exportclt_t exportclt; 

list_t inode_entries;
htable_t htable_inode;
htable_t htable_fid;
uint64_t rozofs_ientries_count = 0;

fuse_ino_t inode_idx = 1;

static void rozofs_ll_init(void *userdata, struct fuse_conn_info *conn) {
    int *piped = (int *) userdata;
    char s;
    (void) conn;
    if (piped[1] >= 0) {
        s = 0;
        if (write(piped[1], &s, 1) != 1) {
            warning("rozofs_ll_init: pipe write error: %s", strerror(errno));
        }
        close(piped[1]);
    }
}

void rozofs_ll_forget(fuse_req_t req, fuse_ino_t ino, unsigned long nlookup) {
    ientry_t *ie;

    START_PROFILING(rozofs_ll_forget);

    DEBUG("forget :%lu, nlookup: %lu", ino, nlookup);
    if ((ie = get_ientry_by_inode(ino))) {
        assert(ie->nlookup >= nlookup);
        DEBUG("forget :%lu, ie lookup: %lu", ino, ie->nlookup);
        if ((ie->nlookup -= nlookup) == 0) {
            DEBUG("del entry for %lu", ino);
            del_ientry(ie);
            if (ie->db.p != NULL) {
                free(ie->db.p);
                ie->db.p = NULL;
            }
            free(ie);
        }
    }

    STOP_PROFILING(rozofs_ll_forget);
    fuse_reply_none(req);
}

/*
 * All below are implemented for monitoring purpose.
 */
#warning fake untested function.

void rozofs_ll_getlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
        struct flock *lock) {
    START_PROFILING(rozofs_ll_getlk);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_getlk);
}

#warning fake untested function.

void rozofs_ll_setlk(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi,
        struct flock *lock, int sleep) {
    START_PROFILING(rozofs_ll_getlk);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_setlk);
}

#warning fake untested function.

void rozofs_ll_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
    START_PROFILING(rozofs_ll_releasedir);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_releasedir);
}

#warning fake untested function.

void rozofs_ll_fsyncdir(fuse_req_t req, fuse_ino_t ino, int datasync,
        struct fuse_file_info *fi) {
    START_PROFILING(rozofs_ll_fsyncdir);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_fsyncdir);
}

#warning fake untested function.

void rozofs_ll_ioctl(fuse_req_t req, fuse_ino_t ino, int cmd, void *arg,
        struct fuse_file_info *fi, unsigned flags,
        const void *in_buf, size_t in_bufsz, size_t out_bufsz) {
    START_PROFILING(rozofs_ll_ioctl);
    fuse_reply_ioctl(req, 0, in_buf, out_bufsz);
    STOP_PROFILING(rozofs_ll_ioctl);
}

#warning fake untested function.

void rozofs_ll_access(fuse_req_t req, fuse_ino_t ino, int mask) {
    START_PROFILING(rozofs_ll_access);
    fuse_reply_err(req, 0);
    STOP_PROFILING(rozofs_ll_access);
}

static SVCXPRT *rozofsmount_create_rpc_service(int port) {
    int sock;
    int one = 1;
    struct sockaddr_in sin;

    /* Give the socket a name. */
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;

    /* Create the socket. */
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        severe("Can't create socket: %s.", strerror(errno));
        return NULL;
    }

    /* Set socket options */
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_DEFER_ACCEPT, (char *) &one, sizeof (int));
    setsockopt(sock, SOL_TCP, TCP_NODELAY, (char *) &one, sizeof (int));

    /* Bind the socket */
    if (bind(sock, (struct sockaddr *) &sin, sizeof (struct sockaddr)) < 0) {
        severe("Couldn't bind to tcp port %d", port);
        return NULL;
    }

    /* Creates a TCP/IP-based RPC service transport */
    return svctcp_create(sock, ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE);

}

void rozofmount_profiling_thread_run(void *args) {

    int *port = args;

    rozofsmount_profile_svc = rozofsmount_create_rpc_service(*port);
    if (!rozofsmount_profile_svc) {
        severe("can't create monitoring service: %s", strerror(errno));
    }

    /* Associates ROZOFSMOUNT_PROFILE_PROGRAM and ROZOFSMOUNT_PROFILE_VERSION
     * with the service dispatch procedure, rozofsmount_profile_program_1.
     * Here protocol is zero, the service is not registered with
     *  the portmap service */
    if (!svc_register(rozofsmount_profile_svc, ROZOFSMOUNT_PROFILE_PROGRAM,
            ROZOFSMOUNT_PROFILE_VERSION,
            rozofsmount_profile_program_1, 0)) {
        severe("can't register service : %s", strerror(errno));
    }

    svc_run();
    DEBUG("REACHED !!!!");
    /* NOT REACHED */
}
#define SHOW_PROFILER_PROBE(probe) pChar += sprintf(pChar," %-12s | %15"PRIu64" | %9"PRIu64" | %18"PRIu64" | %15s |\n",\
                    #probe,\
                    gprofiler.rozofs_ll_##probe[P_COUNT],\
                    gprofiler.rozofs_ll_##probe[P_COUNT]?gprofiler.rozofs_ll_##probe[P_ELAPSE]/gprofiler.rozofs_ll_##probe[P_COUNT]:0,\
                    gprofiler.rozofs_ll_##probe[P_ELAPSE]," " );

#define SHOW_PROFILER_PROBE_BYTE(probe) pChar += sprintf(pChar," %-12s | %15"PRIu64" | %9"PRIu64" | %18"PRIu64" | %15"PRIu64" |\n",\
                    #probe,\
                    gprofiler.rozofs_ll_##probe[P_COUNT],\
                    gprofiler.rozofs_ll_##probe[P_COUNT]?gprofiler.rozofs_ll_##probe[P_ELAPSE]/gprofiler.rozofs_ll_##probe[P_COUNT]:0,\
                    gprofiler.rozofs_ll_##probe[P_ELAPSE],\
                    gprofiler.rozofs_ll_##probe[P_BYTES]);

void show_profiler(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = localBuf;

    time_t elapse;
    int days, hours, mins, secs;

    // Compute uptime for storaged process
    elapse = (int) (time(0) - gprofiler.uptime);
    days = (int) (elapse / 86400);
    hours = (int) ((elapse / 3600) - (days * 24));
    mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
    secs = (int) (elapse % 60);


    pChar += sprintf(pChar, "GPROFILER version %s uptime =  %d days, %d:%d:%d\n", gprofiler.vers,days, hours, mins, secs);
    pChar += sprintf(pChar, " - ientry counter: %llu\n", (long long unsigned int) rozofs_ientries_count);
    pChar += sprintf(pChar, "   procedure  |     count       |  time(us) | cumulated time(us) |     bytes       |\n");
    pChar += sprintf(pChar, "--------------+-----------------+-----------+--------------------+-----------------+\n");
    SHOW_PROFILER_PROBE(lookup);
    SHOW_PROFILER_PROBE(forget);
    SHOW_PROFILER_PROBE(getattr);
    SHOW_PROFILER_PROBE(setattr);
    SHOW_PROFILER_PROBE(readlink);
    SHOW_PROFILER_PROBE(mknod);
    SHOW_PROFILER_PROBE(mkdir);
    SHOW_PROFILER_PROBE(unlink);
    SHOW_PROFILER_PROBE(rmdir);
    SHOW_PROFILER_PROBE(symlink);
    SHOW_PROFILER_PROBE(rename);
    SHOW_PROFILER_PROBE(open);
    SHOW_PROFILER_PROBE(link);
    SHOW_PROFILER_PROBE_BYTE(read);
    SHOW_PROFILER_PROBE_BYTE(write);
    SHOW_PROFILER_PROBE(flush);
    SHOW_PROFILER_PROBE(release);
    SHOW_PROFILER_PROBE(opendir);
    SHOW_PROFILER_PROBE(readdir);
    SHOW_PROFILER_PROBE(releasedir);
    SHOW_PROFILER_PROBE(fsyncdir);
    SHOW_PROFILER_PROBE(statfs);
    SHOW_PROFILER_PROBE(setxattr);
    SHOW_PROFILER_PROBE(getxattr);
    SHOW_PROFILER_PROBE(listxattr);
    SHOW_PROFILER_PROBE(removexattr);
    SHOW_PROFILER_PROBE(access);
    SHOW_PROFILER_PROBE(create);
    SHOW_PROFILER_PROBE(getlk);
    SHOW_PROFILER_PROBE(setlk);
    SHOW_PROFILER_PROBE(ioctl);
    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}

typedef struct _xmalloc_stats_t {
    uint64_t count;
    int size;
} xmalloc_stats_t;

#define XMALLOC_MAX_SIZE  512

extern xmalloc_stats_t *xmalloc_size_table_p;

void show_xmalloc(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = localBuf;
    int i;
    xmalloc_stats_t *p;

    if (xmalloc_size_table_p == NULL) {
        pChar += sprintf(pChar, "xmalloc stats not available\n");
        uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
        return;

    }

    for (i = 0; i < 64; i++) {
        p = &xmalloc_size_table_p[i];
        if (p->size == 0) {
            break;
        }
        pChar += sprintf(pChar, "size %8.8u count %10.10llu \n", p->size, (long long unsigned int) p->count);
    }
    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);

}
static struct fuse_lowlevel_ops rozofs_ll_operations = {
    .init = rozofs_ll_init,
    //.destroy = rozofs_ll_destroy,
    .lookup = rozofs_ll_lookup_nb, /** non blocking */
    .forget = rozofs_ll_forget, /** non blocking by construction */
    .getattr = rozofs_ll_getattr_nb, /** non blocking */
    .setattr = rozofs_ll_setattr_nb, /** non blocking */
    .readlink = rozofs_ll_readlink_nb, /** non blocking */
    .mknod = rozofs_ll_mknod_nb, /** non blocking */
    .mkdir = rozofs_ll_mkdir_nb, /** non blocking */
    .unlink = rozofs_ll_unlink_nb, /** non blocking */
    .rmdir = rozofs_ll_rmdir_nb, /** non blocking */
    .symlink = rozofs_ll_symlink_nb, /** non blocking */
    .rename = rozofs_ll_rename_nb, /** non blocking */
    .open = rozofs_ll_open_nb, /** non blocking */
    .link = rozofs_ll_link_nb, /** non blocking */
    .read = rozofs_ll_read_nb, /**non blocking */
    .write = rozofs_ll_write_nb, /**non blocking */
    .flush = rozofs_ll_flush_nb, /**non blocking */
    .release = rozofs_ll_release_nb, /**non blocking */
    //.opendir = rozofs_ll_opendir, /** non blocking by construction */
    .readdir = rozofs_ll_readdir_nb, /** non blocking */
    //.releasedir = rozofs_ll_releasedir, /** non blocking by construction */
    //.fsyncdir = rozofs_ll_fsyncdir, /** non blocking by construction */
    .statfs = rozofs_ll_statfs_nb, /** non blocking */
    .setxattr = rozofs_ll_setxattr_nb, /** non blocking */
    .getxattr = rozofs_ll_getxattr_nb, /** non blocking */
    .listxattr = rozofs_ll_listxattr_nb, /** non blocking */
    .removexattr = rozofs_ll_removexattr_nb, /** non blocking */
    .access = rozofs_ll_access, /** non blocking by construction */
    .create = rozofs_ll_create_nb, /** non blocking */
    //.getlk = rozofs_ll_getlk,
    //.setlk = rozofs_ll_setlk,
    //.bmap = rozofs_ll_bmap,
    //.ioctl = rozofs_ll_ioctl,
    //.poll = rozofs_ll_poll,
};

void rozofs_kill_storcli(const char *mountpoint) {

    char cmd[1024];
    char *cmd_p = &cmd[0];
    cmd_p += sprintf(cmd_p, "%s %s", STORCLI_KILLER, mountpoint);
    system(cmd);
}

void rozofs_start_storcli(const char *mountpoint) {
    int i;
    char cmd[1024];
    rozofs_kill_storcli(mountpoint);
    for (i = 1; i <= STORCLI_PER_FSMOUNT; i++) {
        char *cmd_p = &cmd[0];
        cmd_p += sprintf(cmd_p, "%s ", STORCLI_STARTER);
        cmd_p += sprintf(cmd_p, "%s ", STORCLI_EXEC);
        cmd_p += sprintf(cmd_p, "-i %d ", i);
        cmd_p += sprintf(cmd_p, "-H %s ", conf.host);
        cmd_p += sprintf(cmd_p, "-E %s ", conf.export);
        cmd_p += sprintf(cmd_p, "-M %s ", mountpoint);
        cmd_p += sprintf(cmd_p, "-D %d ", conf.dbg_port + i);
        cmd_p += sprintf(cmd_p, "-R %d ", conf.instance);
        cmd_p += sprintf(cmd_p, "-s %d ",ROZOFS_TMR_GET(TMR_STORAGE_PROGRAM));
        cmd_p += sprintf(cmd_p, "&");
    info("rozofs_start_storcli  cmd %s",cmd);
        system(cmd);
    }
}

int fuseloop(struct fuse_args *args, const char *mountpoint, int fg) {
    int i = 0;
    int err;
    char *c;
    int piped[2];
    piped[0] = piped[1] = -1;
    char s;
    struct fuse_chan *ch;
    struct fuse_session *se;
    int sock;
    pthread_t profiling_thread;
    uint16_t profiling_port;
    int retry_count;
    char ppfile[NAME_MAX];
    int ppfd = -1;

    openlog("rozofsmount", LOG_PID, LOG_LOCAL0);



    struct timeval timeout_mproto;
    timeout_mproto.tv_sec = ROZOFS_TMR_GET(TMR_EXPORT_PROGRAM);
    timeout_mproto.tv_usec = 0;


    for (retry_count = 3; retry_count > 0; retry_count--) {
        /* Initiate the connection to the export and get informations
         * about exported filesystem */
        /// XXX: TO CHANGE
        if (exportclt_initialize(
                &exportclt,
                conf.host,
                conf.export,
                conf.passwd,
                conf.buf_size * 1024,
                conf.max_retry,
                timeout_mproto) == 0) break;

        sleep(2);
    }

    if (retry_count == 0) {

        fprintf(stderr,
                "rozofsmount failed for:\n" "export directory: %s\n"
                "export hostname: %s\n" "local mountpoint: %s\n" "error: %s\n"
                "See log for more information\n", conf.export, conf.host,
                mountpoint, strerror(errno));
        return 1;
    }

    /* Initialize list and htables for inode_entries */
    list_init(&inode_entries);
    htable_initialize(&htable_inode, INODE_HSIZE, fuse_ino_hash, fuse_ino_cmp);
    htable_initialize(&htable_fid, PATH_HSIZE, fid_hash, fid_cmp);

    /* Put the root inode entry*/
    ientry_t *root = xmalloc(sizeof (ientry_t));
    memset(root, 0, sizeof (ientry_t));
    memcpy(root->fid, exportclt.rfid, sizeof (fid_t));
    root->inode = next_inode_idx();
    root->db.size = 0;
    root->db.eof = 0;
    root->db.p = NULL;
    root->nlookup = 1;
    put_ientry(root);

    info("mounting - export: %s from : %s on: %s", conf.export, conf.host,
            mountpoint);

    if (fg == 0) {
        if (pipe(piped) < 0) {
            fprintf(stderr, "pipe error\n");
            return 1;
        }
        err = fork();
        if (err < 0) {
            fprintf(stderr, "fork error\n");
            return 1;
        } else if (err > 0) {
            // Parent process closes up output side of pipe
            close(piped[1]);
            err = read(piped[0], &s, 1);
            if (err == 0) {
                s = 1;
            }
            return s;
        }
        // Child process closes up input side of pipe
        close(piped[0]);
        s = 1;
    }
    if ((ch = fuse_mount(mountpoint, args)) == NULL) {
        fprintf(stderr, "error in fuse_mount\n");
        if (piped[1] >= 0) {
            if (write(piped[1], &s, 1) != 1) {
                fprintf(stderr, "pipe write error\n");
            }
            close(piped[1]);
        }
        return 1;
    }

    se = fuse_lowlevel_new(args, &rozofs_ll_operations,
            sizeof (rozofs_ll_operations), (void *) piped);

    if (se == NULL) {
        fuse_unmount(mountpoint, ch);
        fprintf(stderr, "error in fuse_lowlevel_new\n");
        usleep(100000); // time for print other error messages by FUSE
        if (piped[1] >= 0) {
            if (write(piped[1], &s, 1) != 1) {
                fprintf(stderr, "pipe write error\n");
            }
            close(piped[1]);
        }
        return 1;
    }

    fuse_session_add_chan(se, ch);

    if (fuse_set_signal_handlers(se) < 0) {
        fprintf(stderr, "error in fuse_set_signal_handlers\n");
        fuse_session_remove_chan(ch);
        fuse_session_destroy(se);
        fuse_unmount(mountpoint, ch);
        if (piped[1] >= 0) {
            if (write(piped[1], &s, 1) != 1) {
                fprintf(stderr, "pipe write error\n");
            }
            close(piped[1]);
        }
        return 1;
    }

    if (fg == 0) {
        setsid();
        setpgid(0, getpid());
        if ((i = open("/dev/null", O_RDWR, 0)) != -1) {
            (void) dup2(i, STDIN_FILENO);
            (void) dup2(i, STDOUT_FILENO);
            (void) dup2(i, STDERR_FILENO);
            if (i > 2)
                close(i);
        }
    }

    /* Creates one debug thread */

    pthread_t thread;
    rozofs_fuse_conf_t rozofs_fuse_conf;

    semForEver = malloc(sizeof (sem_t)); /* semaphore for blocking the main thread doing nothing */
    //    int ret;

    if (sem_init(semForEver, 0, 0) == -1) {
        severe("main() sem_init() problem : %s", strerror(errno));
    }


    /*
     ** Register these topics befora start the rozofs_stat_start that will
     ** register other topic. Topic registration is not safe in multi thread
     ** case
     */
    uma_dbg_addTopic("profiler", show_profiler);
    uma_dbg_addTopic("xmalloc", show_xmalloc);
    /*
    ** declare timer debug functions
    */
    rozofs_timer_conf_dbg_init();
    /*
    ** Check if the base port of rozodebug has been provided, if there is no value, set it to default
    */
    if (conf.dbg_port == 0) 
    {
      conf.dbg_port = rzdbg_default_base_port;    
    }
    else
    {
      rzdbg_default_base_port = conf.dbg_port;    
    }    
    rozofs_fuse_conf.instance = (uint16_t) conf.instance;
    rozofs_fuse_conf.debug_port = (uint16_t)rzdbg_get_rozofsmount_port((uint16_t) conf.instance);
    conf.dbg_port = rozofs_fuse_conf.debug_port;
    rozofs_fuse_conf.se = se;
    rozofs_fuse_conf.ch = ch;
    rozofs_fuse_conf.exportclt = (void*) &exportclt;
    rozofs_fuse_conf.max_transactions = 32;

    if ((errno = pthread_create(&thread, NULL, (void*) rozofs_stat_start, &rozofs_fuse_conf)) != 0) {
        severe("can't create debug thread: %s", strerror(errno));
        return err;
    }

    /*
     * Start profiling server
     */
    gprofiler.uptime = time(0);
    strncpy((char *) gprofiler.vers, VERSION, 20);
    /* Find a free port */
    for (profiling_port = 50000; profiling_port < 60000; profiling_port++) {
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            severe("can't create socket: %s", strerror(errno));
            break;
        }
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = htons(profiling_port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if ((bind(sock, (struct sockaddr *) &addr, sizeof (struct sockaddr_in))) != 0) {
            if (errno == EADDRINUSE)
                profiling_port++; /* Try next port */
            else {
                severe("can't bind socket: %s", strerror(errno));
                close(sock);
            }
        } else {
            shutdown(sock, 2);
            close(sock);
            break;
        }
    }

    if (profiling_port >= 60000) {
        severe("no free port for monitoring !");
    } else {
        if ((errno = pthread_create(&profiling_thread, NULL,
                (void*) rozofmount_profiling_thread_run,
                &profiling_port)) != 0) {
            severe("can't create monitoring thread: %s", strerror(errno));
        }
    }
    info("monitoring port: %d", profiling_port);

    /* try to create a flag file with port number */
    sprintf(ppfile, "%s%s_%d%s", DAEMON_PID_DIRECTORY, "rozofsmount",conf.instance, mountpoint);
    c = ppfile + strlen(DAEMON_PID_DIRECTORY);
    while (*c++) {
        if (*c == '/') *c = '.';
    }
    if ((ppfd = open(ppfile, O_RDWR | O_CREAT, 0640)) < 0) {
        severe("can't open profiling port file");
    } else {
        char str[10];
        sprintf(str, "%d\n", getpid());
        write(ppfd, str, strlen(str));
        close(ppfd);
    }

    rozofs_start_storcli(mountpoint);

    for (;;) {
        int ret;
        ret = sem_wait(semForEver);
        if (ret != 0) {
            severe("sem_wait: %s", strerror(errno));
            continue;

        }
        break;
    }

    rozofs_kill_storcli(mountpoint);

    fuse_remove_signal_handlers(se);
    fuse_session_remove_chan(ch);
    fuse_session_destroy(se);
    fuse_unmount(mountpoint, ch);
    exportclt_release(&exportclt);
    ientries_release();
    rozofs_layout_release();
    if (conf.export != NULL)
        free(conf.export);
    if (conf.host != NULL)
        free(conf.host);
    if (conf.passwd != NULL)
        free(conf.passwd);
    unlink(ppfile); // best effort

    return err ? 1 : 0;
}

int main(int argc, char *argv[]) {
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    char *mountpoint;
    int fg = 0;
    int res;

    memset(&conf, 0, sizeof (conf));
    /*
    ** init of the timer configuration
    */
    rozofs_tmr_init_configuration();

    conf.max_retry = 50;
    conf.buf_size = 0;

    if (fuse_opt_parse(&args, &conf, rozofs_opts, myfs_opt_proc) < 0) {
        exit(1);
    }

    if (conf.host == NULL) {
        conf.host = strdup("rozofsexport");
    }

    if (strlen(conf.host) >= ROZOFS_HOSTNAME_MAX) {
        fprintf(stderr,
                "The length of export host must be lower than %d\n",
                ROZOFS_HOSTNAME_MAX);
    }

    if (conf.export == NULL) {
        conf.export = strdup("/srv/rozofs/exports/export");
    }

    if (conf.passwd == NULL) {
        conf.passwd = strdup("none");
    }

    if (conf.buf_size == 0) {
        conf.buf_size = 256;
    }
    if (conf.buf_size < 128) {
        fprintf(stderr,
                "write cache size too low (%u KiB) - increased to 128 KiB\n",
                conf.buf_size);
        conf.buf_size = 128;
    }
    if (conf.buf_size > 8192) {
        fprintf(stderr,
                "write cache size too big (%u KiB) - decreased to 8192 KiB\n",
                conf.buf_size);
        conf.buf_size = 8192;
    }


    // Set timeout for exportd requests
    if (conf.export_timeout != 0) {
        if (rozofs_tmr_configure(TMR_EXPORT_PROGRAM,conf.export_timeout)< 0)
        {
          fprintf(stderr,
                "timeout for exportd requests is out of range: revert to default setting");
        }
    }

    if (conf.storage_timeout != 0) {
        if (rozofs_tmr_configure(TMR_STORAGE_PROGRAM,conf.storage_timeout)< 0)
        {
          fprintf(stderr,
                "timeout for storaged requests is out of range: revert to default setting");
        }
    }

    if (conf.storcli_timeout != 0) {
        if (rozofs_tmr_configure(TMR_STORCLI_PROGRAM,conf.storcli_timeout)< 0)
        {
          fprintf(stderr,
                "timeout for storcli requests is out of range: revert to default setting");
        }
    }

    if (fuse_version() < 28) {
        if (fuse_opt_add_arg(&args, "-o" FUSE27_DEFAULT_OPTIONS) == -1) {
            fprintf(stderr, "fuse_opt_add_arg failed\n");
            return 1;
        }
    } else {
        if (fuse_opt_add_arg(&args, "-o" FUSE28_DEFAULT_OPTIONS) == -1) {
            fprintf(stderr, "fuse_opt_add_arg failed\n");
            return 1;
        }
    }

    if (fuse_parse_cmdline(&args, &mountpoint, NULL, &fg) == -1) {
        fprintf(stderr, "see: %s -h for help\n", argv[0]);
        return 1;
    }

    if (!mountpoint) {
        fprintf(stderr, "no mount point\nsee: %s -h for help\n", argv[0]);
        return 1;
    }

    res = fuseloop(&args, mountpoint, fg);

    fuse_opt_free_args(&args);
    return res;
}
