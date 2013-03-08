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
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/types.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/rpc/mpproto.h>
#include <rozofs/rpc/spclient.h>
#include <rozofs/rpc/mpclient.h>
#include <rozofs/rpc/epclient.h>

#include "config.h"

static char *profiled_host;

static int profiling_port = 0;

static char *profiled_service;

static char *profiling_cmde;

static int profiling_watch = 0;

static union {
    sp_client_t sp[STORAGE_NODE_PORTS_MAX + 1 + STORAGES_MAX_BY_STORAGE_NODE];
    mp_client_t mp;
    ep_client_t ep;
} profiler_client;

void usage() {
    printf("profils RozoFS daemons - %s\n", VERSION);
    printf("Usage: rprof [OPTIONS] <service@hostname[:port]> <display|clear>\n\n");
    printf("\t-h, --help\tprint this message.\n");
}

/*
 * storaged profiling
 */
static void connect_to_storaged_profil() {
    int i, j;
    spp_profiler_t profiler;
    strcpy(profiler_client.sp[0].host, profiled_host);
    profiler_client.sp[0].port = profiling_port;

    // Connect to master process
    if (sp_client_initialize(&profiler_client.sp[0]) != 0) {
        fprintf(stderr,
                "failed to connect (master service profiling) to %s: %s\n",
                profiled_host, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Need to get monitor values (nb_io_processes and nb_rb_processes)
    if (sp_client_get_profiler(&profiler_client.sp[0], &profiler) != 0) {
        fprintf(stderr, "failed to get master profiler from %s: %s\n",
                profiled_host, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Establishing a connection for each io process
    for (i = 0; i < profiler.nb_io_processes; i++) {
        strcpy(profiler_client.sp[i + 1].host, profiled_host);
        profiler_client.sp[i + 1].port = profiler.io_process_ports[i];

        if (sp_client_initialize(&profiler_client.sp[i + 1]) != 0) {
            fprintf(stderr,
                    "failed to connect (rbs service profiling) to %s: %s\n",
                    profiled_host, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }

    // Establishing a connection for each rebuild process
    for (i = 0; i < profiler.nb_rb_processes; i++) {

        j = profiler.nb_io_processes + i + 1;

        strcpy(profiler_client.sp[j].host, profiled_host);
        profiler_client.sp[j].port = profiler.rb_process_ports[i];

        if (sp_client_initialize(&profiler_client.sp[j]) != 0) {
            // Here error is considered normal because it's possible that the
            // rebuild process is completed the rebuild
            profiler_client.sp[j].port = 0;
        }
    }
}

#define sp_display_rbs_finish_probe(the_port, the_cid, the_sid)\
    {\
        fprintf(stdout,\
                "%-12"PRIu16" %-12"PRIu16" %-12"PRIu8" %-16s %s/%-12s\n",\
                the_port, the_cid, the_sid, "completed", "---", "---");\
    }

#define sp_display_rbs_progress_probe(the_port, the_cid, the_sid, the_profiler,\
                                        the_probe_1, the_probe_2)\
    {\
        fprintf(stdout,\
                "%-12"PRIu16" %-12"PRIu16" %-12"PRIu8" %-16s"\
                "%"PRIu64"/%-12"PRIu64"\n",\
                the_port, the_cid, the_sid, "in progress",\
                the_profiler.the_probe_2, the_profiler.the_probe_1);\
    }

#define sp_display_probe(the_profiler, the_probe)\
    {\
        uint64_t rate;\
        uint64_t cpu;\
        if (the_profiler.the_probe[P_COUNT] == 0) {\
            cpu = rate = 0;\
        } else {\
            rate = (the_profiler.the_probe[P_COUNT] * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
            cpu = the_profiler.the_probe[P_ELAPSE] / the_profiler.the_probe[P_COUNT];\
        }\
        fprintf(stdout, "%-12s %-16s %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12s %-12s\n",\
                "--", #the_probe, the_profiler.the_probe[P_COUNT], \
                rate, cpu, "--", "--");\
    }

#define sp_display_io_probe(the_port, the_profiler, the_probe)\
    {\
        uint64_t rate;\
        uint64_t cpu;\
        uint64_t throughput;\
        if (the_profiler.the_probe[P_COUNT] == 0) {\
            cpu = rate = throughput = 0;\
        } else {\
            rate = (the_profiler.the_probe[P_COUNT] * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
            cpu = the_profiler.the_probe[P_ELAPSE] / the_profiler.the_probe[P_COUNT];\
            throughput = (the_profiler.the_probe[P_BYTES] / 1024 /1024 * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
        }\
        fprintf(stdout, "%-12"PRIu16" %-16s %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12"PRIu64"\n",\
                the_port, #the_probe, the_profiler.the_probe[P_COUNT], \
                rate, cpu, the_profiler.the_probe[P_BYTES], throughput);\
    }

static void profile_storaged_display() {
    time_t elapse;
    int i, j, days, hours, mins, secs;
    spp_profiler_t profiler;

    // Get profiler from master storaged process
    if (sp_client_get_profiler(&profiler_client.sp[0], &profiler) != 0) {
        perror("failed to to get profiler");
        exit(EXIT_FAILURE);
    }

    // Compute uptime for storaged process
    elapse = (int) (profiler.now - profiler.uptime);
    days = (int) (elapse / 86400);
    hours = (int) ((elapse / 3600) - (days * 24));
    mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
    secs = (int) (elapse % 60);

    // Print general profiling values for storaged
    fprintf(stdout, "storaged: %s - %"PRIu16" IO process(es),"
            " uptime: %d days, %d:%d:%d\n",
            profiler.vers, profiler.nb_io_processes, days, hours, mins, secs);

    // Print header for operations profiling values for storaged
    fprintf(stdout, "%-12s %-16s %-12s %-12s %-12s %-12s %-12s\n", "PORT", "OP",
            "CALL", "RATE(msg/s)", "CPU(us)", "COUNT(B)", "THROUGHPUT(MB/s)");

    // Print master storaged process profiling values
    sp_display_probe(profiler, stat);
    sp_display_probe(profiler, ports);
    sp_display_probe(profiler, remove);

    // Print IO storaged process profiling values
    for (i = 0; i < profiler.nb_io_processes; i++) {
        spp_profiler_t sp;

        if (sp_client_get_profiler(&profiler_client.sp[i + 1], &sp) != 0) {
            perror("failed to get profile");
            exit(EXIT_FAILURE);
        }
        sp_display_io_probe(profiler.io_process_ports[i], sp, read);
        sp_display_io_probe(profiler.io_process_ports[i], sp, write);
        sp_display_io_probe(profiler.io_process_ports[i], sp, truncate);
    }

    // Print RBS storaged process profiling values
    if (profiler.nb_rb_processes != 0) {
        fprintf(stdout, "Nb. of storage(s) to rebuild at startup: %"PRIu16"\n",
                profiler.nb_rb_processes);

        fprintf(stdout, "%-12s %-12s %-12s %-16s %-16s\n", "PORT", "CID", "SID",
                "STATUS", "FILES REBUILD");

        // Print RBS storaged process profiling values
        // for each storage to rebuild
        for (i = 0; i < profiler.nb_rb_processes; i++) {
            spp_profiler_t sp;

            j = profiler.nb_io_processes + i + 1;

            // If port == 0
            // The rebuild process is completed
            if (profiler_client.sp[j].port == 0) {
                sp_display_rbs_finish_probe(profiler.rb_process_ports[i],
                        profiler.rbs_cids[i], profiler.rbs_sids[i]);
                continue;
            }

            // The rebuild process is not completed
            if (sp_client_get_profiler(&profiler_client.sp[j], &sp) != 0) {

                if (profiling_watch) {
                    sp_display_rbs_finish_probe(profiler.rb_process_ports[i],
                            profiler.rbs_cids[i], profiler.rbs_sids[i]);
                    continue;
                } else {
                    fprintf(stderr,
                            "failed to get master profiler: %s\n",
                            strerror(errno));
                }
            }

            sp_display_rbs_progress_probe(profiler.rb_process_ports[i],
                    profiler.rbs_cids[i], profiler.rbs_sids[i], sp,
                    rb_files_total, rb_files_current);
        }
    }
}

static void profile_storaged_clear() {
    int i;
    spp_profiler_t sp;

    if (sp_client_clear(&profiler_client.sp[0]) != 0) {
        perror("failed to clear monitor");
        exit(EXIT_FAILURE);
    }

    if (sp_client_get_profiler(&profiler_client.sp[0], &sp) != 0) {
        perror("failed to to get monitor");
        exit(EXIT_FAILURE);
    }

    for (i = 0; i < sp.nb_io_processes; i++) {

        if (sp_client_clear(&profiler_client.sp[i + 1]) != 0) {

            perror("failed to clear monitor");
            exit(EXIT_FAILURE);
        }
    }
    // Here no sense to clear rbs process profiling values
}

static void profile_storaged() {
    /* check valid cmdes for storaged */
    if (strcmp(profiling_cmde, "display") != 0 && strcmp(profiling_cmde, "clear") != 0) {
        usage();
        exit(EXIT_FAILURE);
    }
    connect_to_storaged_profil();

    if (strcmp(profiling_cmde, "display") == 0) {
        do {
            profile_storaged_display();
            if (profiling_watch)
                sleep(1);
        } while (profiling_watch);
    } else if (strcmp(profiling_cmde, "clear") == 0) {

        profile_storaged_clear();
        profile_storaged_display();
    }
}

/*
 * rozofsmount profiling
 */
static void connect_to_rozofsmount_profil() {
    strcpy(profiler_client.mp.host, profiled_host);
    profiler_client.mp.port = profiling_port;
    if (mp_client_initialize(&profiler_client.mp) != 0) {
        fprintf(stderr, "failed to connect to %s: %s\n", profiled_host,
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

#define mp_display_probe(the_profiler, the_probe)\
    {\
        uint64_t rate;\
        uint64_t cpu;\
        if (the_profiler.the_probe[P_COUNT] == 0) {\
            cpu = rate = 0;\
        } else {\
            rate = (the_profiler.the_probe[P_COUNT] * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
            cpu = the_profiler.the_probe[P_ELAPSE] / the_profiler.the_probe[P_COUNT];\
        }\
        fprintf(stdout, "%-25s %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12s %-12s\n",\
                #the_probe, the_profiler.the_probe[P_COUNT], \
                rate, cpu, "--", "--");\
    }

#define mp_display_io_probe(the_profiler, the_probe)\
    {\
        uint64_t rate;\
        uint64_t cpu;\
        uint64_t throughput;\
        if (the_profiler.the_probe[P_COUNT] == 0) {\
            cpu = rate = throughput = 0;\
        } else {\
            rate = (the_profiler.the_probe[P_COUNT] * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
            cpu = the_profiler.the_probe[P_ELAPSE] / the_profiler.the_probe[P_COUNT];\
            throughput = (the_profiler.the_probe[P_BYTES] / 1024 /1024 * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
        }\
        fprintf(stdout, "%-25s %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12"PRIu64"\n",\
                #the_probe, the_profiler.the_probe[P_COUNT], \
                rate, cpu, the_profiler.the_probe[P_BYTES], throughput);\
    }

static void profile_rozofsmount_display() {
    time_t elapse;
    int days, hours, mins, secs;
    mpp_profiler_t mp;

    if (mp_client_get_profiler(&profiler_client.mp, &mp) != 0) {

        perror("failed to to get monitor");
        exit(EXIT_FAILURE);
    }

    elapse = (int) (mp.now - mp.uptime);
    days = (int) (elapse / 86400);
    hours = (int) ((elapse / 3600) - (days * 24));
    mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
    secs = (int) (elapse % 60);
    fprintf(stdout, "rozofsmount: %s - uptime: %d days, %d:%d:%d\n",
            mp.vers, days, hours, mins, secs);

    fprintf(stdout, "%-25s %-12s %-12s %-12s %-12s %-12s\n", "OP",
            "CALL", "RATE(msg/s)", "CPU(us)", "COUNT(B)", "THROUGHPUT(MB/s)");

    mp_display_probe(mp, rozofs_ll_lookup);
    mp_display_probe(mp, rozofs_ll_forget);
    mp_display_probe(mp, rozofs_ll_getattr);
    mp_display_probe(mp, rozofs_ll_setattr);
    mp_display_probe(mp, rozofs_ll_readlink);
    mp_display_probe(mp, rozofs_ll_mknod);
    mp_display_probe(mp, rozofs_ll_mkdir);
    mp_display_probe(mp, rozofs_ll_unlink);
    mp_display_probe(mp, rozofs_ll_rmdir);
    mp_display_probe(mp, rozofs_ll_symlink);
    mp_display_probe(mp, rozofs_ll_rename);
    mp_display_probe(mp, rozofs_ll_open);
    mp_display_probe(mp, rozofs_ll_link);
    mp_display_io_probe(mp, rozofs_ll_read);
    mp_display_io_probe(mp, rozofs_ll_write);
    mp_display_probe(mp, rozofs_ll_flush);
    mp_display_probe(mp, rozofs_ll_release);
    mp_display_probe(mp, rozofs_ll_opendir);
    mp_display_probe(mp, rozofs_ll_readdir);
    mp_display_probe(mp, rozofs_ll_releasedir);
    mp_display_probe(mp, rozofs_ll_fsyncdir);
    mp_display_probe(mp, rozofs_ll_statfs);
    mp_display_probe(mp, rozofs_ll_setxattr);
    mp_display_probe(mp, rozofs_ll_getxattr);
    mp_display_probe(mp, rozofs_ll_listxattr);
    mp_display_probe(mp, rozofs_ll_removexattr);
    mp_display_probe(mp, rozofs_ll_access);
    mp_display_probe(mp, rozofs_ll_create);
    mp_display_probe(mp, rozofs_ll_getlk);
    mp_display_probe(mp, rozofs_ll_setlk);
    mp_display_probe(mp, rozofs_ll_ioctl);
}

static void profile_rozofsmount_clear() {

    if (mp_client_clear(&profiler_client.mp) != 0) {

        perror("failed to clear profil");
        exit(EXIT_FAILURE);
    }
}

static void profile_rozofsmount(char *host, char *cmde) {
    /* check valid cmdes for storaged */
    if (strcmp(profiling_cmde, "display") != 0 && strcmp(profiling_cmde, "clear") != 0) {
        usage();
        exit(EXIT_FAILURE);
    }
    connect_to_rozofsmount_profil();

    if (strcmp(profiling_cmde, "display") == 0) {
        do {
            profile_rozofsmount_display();
            sleep(1);
        } while (profiling_watch);
    } else if (strcmp(profiling_cmde, "clear") == 0) {

        profile_rozofsmount_clear();
        profile_rozofsmount_display();
    }
}

/*
 * exportd profiling
 */
static void connect_to_exportd_profil() {
    strcpy(profiler_client.ep.host, profiled_host);
    profiler_client.ep.port = profiling_port;
    if (ep_client_initialize(&profiler_client.ep) != 0) {

        fprintf(stderr, "failed to connect to %s: %s\n", profiled_host, strerror(errno));
        exit(EXIT_FAILURE);
    }
}

#define ep_display_probe(the_profiler, the_probe)\
    {\
        uint64_t rate;\
        uint64_t cpu;\
        if (the_profiler.the_probe[P_COUNT] == 0) {\
            cpu = rate = 0;\
        } else {\
            rate = (the_profiler.the_probe[P_COUNT] * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
            cpu = the_profiler.the_probe[P_ELAPSE] / the_profiler.the_probe[P_COUNT];\
        }\
        fprintf(stdout, "%-25s %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12s %-12s\n",\
                #the_probe, the_profiler.the_probe[P_COUNT], \
                rate, cpu, "--", "--");\
    }

#define ep_display_io_probe(the_profiler, the_probe)\
    {\
        uint64_t rate;\
        uint64_t cpu;\
        uint64_t throughput;\
        if (the_profiler.the_probe[P_COUNT] == 0) {\
            cpu = rate = throughput = 0;\
        } else {\
            rate = (the_profiler.the_probe[P_COUNT] * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
            cpu = the_profiler.the_probe[P_ELAPSE] / the_profiler.the_probe[P_COUNT];\
            throughput = (the_profiler.the_probe[P_BYTES] / 1024 /1024 * 1000000 / the_profiler.the_probe[P_ELAPSE]);\
        }\
        fprintf(stdout, "%-25s %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12"PRIu64"\n",\
                #the_probe, the_profiler.the_probe[P_COUNT], \
                rate, cpu, the_profiler.the_probe[P_BYTES], throughput);\
    }

static void profile_exportd_display() {
    time_t elapse;
    int days, hours, mins, secs, i, j;
    epp_profiler_t ep;

    if (ep_client_get_profiler(&profiler_client.ep, &ep) != 0) {
        perror("failed to to get monitor");
        exit(EXIT_FAILURE);
    }
    elapse = (int) (ep.now - ep.uptime);
    days = (int) (elapse / 86400);
    hours = (int) ((elapse / 3600) - (days * 24));
    mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
    secs = (int) (elapse % 60);
    fprintf(stdout, "exportd: %s - uptime: %d days, %d:%d:%d\n",
            ep.vers, days, hours, mins, secs);
    fprintf(stdout, "\nSTATS:\n");
    fprintf(stdout, "------\n");
    for (i = 0; i < ep.nb_volumes; i++) {
        fprintf(stdout, "VOLUME: %d - BSIZE: %d ,BFREE: %"PRIu64"\n",
                ep.vstats[i].vid, ep.vstats[i].bsize, ep.vstats[i].bfree);
        fprintf(stdout, "\n\t%-6s %-6s %-20s %-20s\n", "SID", "STATUS", "CAPACITY(B)",
                "FREE(B)");
        for (j = 0; j < ep.vstats[i].nb_storages; j++) {
            fprintf(stdout, "\t%-6d %-6d %-20"PRIu64" %-20"PRIu64"\n", ep.vstats[i].sstats[j].sid,
                    ep.vstats[i].sstats[j].status, ep.vstats[i].sstats[j].size,
                    ep.vstats[i].sstats[j].free);
        }
        fprintf(stdout, "\n\t%-6s %-6s %-12s %-12s %-12s %-12s\n", "EID", "BSIZE",
                "BLOCKS", "BFREE", "FILES", "FFREE");
        for (j = 0; j < ep.nb_exports; j++) {

            if (ep.estats[j].vid == ep.vstats[i].vid)
                fprintf(stdout, "\t%-6d %-6d %-12"PRIu64" %-12"PRIu64" %-12"PRIu64" %-12"PRIu64"\n", ep.estats[j].eid,
                    ep.estats[j].bsize, ep.estats[j].blocks, ep.estats[j].bfree,
                    ep.estats[j].files, ep.estats[j].ffree);
        }
        fprintf(stdout, "\n");
    }

    fprintf(stdout, "PROFILING:\n");
    fprintf(stdout, "----------\n");
    fprintf(stdout, "%-25s %-12s %-12s %-12s %-12s %-12s\n", "OP",
            "CALL", "RATE(msg/s)", "CPU(us)", "COUNT(B)", "THROUGHPUT(MBps)");

    ep_display_probe(ep, ep_mount);
    ep_display_probe(ep, ep_umount);
    ep_display_probe(ep, ep_statfs);
    ep_display_probe(ep, ep_lookup);
    ep_display_probe(ep, ep_getattr);
    ep_display_probe(ep, ep_setattr);
    ep_display_probe(ep, ep_readlink);
    ep_display_probe(ep, ep_mknod);
    ep_display_probe(ep, ep_mkdir);
    ep_display_probe(ep, ep_unlink);
    ep_display_probe(ep, ep_rmdir);
    ep_display_probe(ep, ep_symlink);
    ep_display_probe(ep, ep_rename);
    ep_display_probe(ep, ep_readdir);
    ep_display_io_probe(ep, ep_read_block);
    ep_display_io_probe(ep, ep_write_block);
    ep_display_probe(ep, ep_link);
    ep_display_probe(ep, ep_setxattr);
    ep_display_probe(ep, ep_getxattr);
    ep_display_probe(ep, ep_removexattr);
    ep_display_probe(ep, ep_listxattr);
    ep_display_probe(ep, export_lv1_resolve_entry);
    ep_display_probe(ep, export_lv2_resolve_path);
    ep_display_probe(ep, export_lookup_fid);
    ep_display_probe(ep, export_update_files);
    ep_display_probe(ep, export_update_blocks);
    ep_display_probe(ep, export_stat);
    ep_display_probe(ep, export_lookup);
    ep_display_probe(ep, export_getattr);
    ep_display_probe(ep, export_setattr);
    ep_display_probe(ep, export_link);
    ep_display_probe(ep, export_mknod);
    ep_display_probe(ep, export_mkdir);
    ep_display_probe(ep, export_unlink);
    ep_display_probe(ep, export_rmdir);
    ep_display_probe(ep, export_symlink);
    ep_display_probe(ep, export_readlink);
    ep_display_probe(ep, export_rename);
    ep_display_io_probe(ep, export_read);
    ep_display_probe(ep, export_read_block);
    ep_display_probe(ep, export_write_block);
    ep_display_probe(ep, export_readdir);
    ep_display_probe(ep, export_setxattr);
    ep_display_probe(ep, export_getxattr);
    ep_display_probe(ep, export_removexattr);
    ep_display_probe(ep, export_listxattr);
    ep_display_probe(ep, lv2_cache_put);
    ep_display_probe(ep, lv2_cache_get);
    ep_display_probe(ep, lv2_cache_del);
    ep_display_probe(ep, volume_balance);
    ep_display_probe(ep, volume_distribute);
    ep_display_probe(ep, volume_stat);
    ep_display_probe(ep, mdir_open);
    ep_display_probe(ep, mdir_close);
    ep_display_probe(ep, mdir_read_attributes);
    ep_display_probe(ep, mdir_write_attributes);
    ep_display_probe(ep, mreg_open);
    ep_display_probe(ep, mreg_close);
    ep_display_probe(ep, mreg_read_attributes);
    ep_display_probe(ep, mreg_write_attributes);
    ep_display_probe(ep, mreg_read_dist);
    ep_display_probe(ep, mreg_write_dist);
    ep_display_probe(ep, mslnk_open);
    ep_display_probe(ep, mslnk_close);
    ep_display_probe(ep, mdir_close);
    ep_display_probe(ep, mslnk_read_attributes);
    ep_display_probe(ep, mslnk_read_link);
    ep_display_probe(ep, mslnk_write_link);
}

static void profile_exportd_clear() {

    if (ep_client_clear(&profiler_client.ep) != 0) {

        perror("failed to clear profil");
        exit(EXIT_FAILURE);
    }
}

static void profile_exportd(char *host, char *cmde) {
    /* check valid cmdes for storaged */
    if (strcmp(profiling_cmde, "display") != 0 && strcmp(profiling_cmde, "clear") != 0) {
        usage();
        exit(EXIT_FAILURE);
    }
    connect_to_exportd_profil();

    if (strcmp(profiling_cmde, "display") == 0) {
        do {
            profile_exportd_display();
            sleep(1);
        } while (profiling_watch);
    } else if (strcmp(profiling_cmde, "clear") == 0) {

        profile_exportd_clear();
        profile_exportd_display();
    }
}

int main(int argc, char **argv) {
    int c;

    while (1) {
        static struct option long_options[] = {
            {"help", no_argument, 0, 'h'},
            {"watch", no_argument, 0, 'w'},
            {0, 0, 0, 0}
        };
        /* getopt_long stores the option index here. */
        int option_index = 0;

        c = getopt_long(argc, argv, "hw", long_options, &option_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
            case 'h':
                usage();
                exit(EXIT_SUCCESS);
                break;
            case 'w':
                profiling_watch = 1;
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

    if (argc - optind < 2) {
        usage();
        exit(EXIT_FAILURE);
    }

    if (!strchr(argv[optind], '@')) {
        usage();
        exit(EXIT_FAILURE);
    };

    profiled_service = strtok(argv[optind], "@");
    profiled_host = strtok(0, "@");
    profiling_port = 0;
    if (index(profiled_host, ':')) {
        profiled_host = strtok(profiled_host, ":");
        profiling_port = atoi(strtok(0, ":"));
    }
    profiling_cmde = argv[optind + 1];

    if (strcmp(profiled_service, "storaged") == 0) {
        profile_storaged(profiled_host, profiling_cmde);
    } else if (strcmp(profiled_service, "rozofsmount") == 0) {
        profile_rozofsmount(profiled_host, profiling_cmde);
    } else if (strcmp(profiled_service, "exportd") == 0) {
        profile_exportd(profiled_host, profiling_cmde);
    } else {
        usage();
        exit(EXIT_FAILURE);
    }

    exit(0);
}
