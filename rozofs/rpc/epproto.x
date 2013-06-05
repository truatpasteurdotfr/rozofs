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

/* Exportd profiling protocol
 */
%#include <rozofs/rozofs.h>


#define EPP_MAX_VOLUMES     16
#define EPP_MAX_STORAGES    2048
#define EPP_MAX_EXPORTS     2048

enum epp_status_t {
    EPP_SUCCESS = 0,
    EPP_FAILURE = 1
};

union epp_status_ret_t switch (epp_status_t status) {
    case EPP_FAILURE:   int error;
    default:            void;
};

struct epp_estat_t {
    uint32_t    eid;
    uint32_t    vid;
    uint16_t    bsize;
    uint64_t    blocks;
    uint64_t    bfree;
    uint64_t    files;
    uint64_t    ffree;
};

struct epp_sstat_t {
    uint16_t    cid;
    uint16_t    sid;
    uint8_t     status;
    uint64_t    size;
    uint64_t    free;
};

struct epp_vstat_t {
    uint16_t    vid;
    uint16_t    bsize;
    uint64_t    bfree;
    uint64_t    blocks;    
    uint32_t    nb_storages;
    epp_sstat_t sstats[EPP_MAX_STORAGES];
};

struct epp_profiler_t {
    uint64_t    uptime;
    uint64_t    now;
    uint8_t     vers[20];
    uint32_t    nb_volumes;
    epp_vstat_t vstats[EPP_MAX_VOLUMES];
    uint32_t    nb_exports;
    epp_estat_t estats[EPP_MAX_EXPORTS];
    uint64_t    ep_mount[2];
    uint64_t    ep_umount[2];
    uint64_t    ep_statfs[2];
    uint64_t    ep_lookup[2];
    uint64_t    ep_getattr[2];
    uint64_t    ep_setattr[2];
    uint64_t    ep_readlink[2];
    uint64_t    ep_mknod[2];
    uint64_t    ep_mkdir[2];
    uint64_t    ep_unlink[2];
    uint64_t    ep_rmdir[2];
    uint64_t    ep_symlink[2];
    uint64_t    ep_rename[2];
    uint64_t    ep_readdir[2];
    uint64_t    ep_read_block[3];
    uint64_t    ep_write_block[3];
    uint64_t    ep_link[2];
    uint64_t    ep_setxattr[2];
    uint64_t    ep_getxattr[2];
    uint64_t    ep_removexattr[2];
    uint64_t    ep_listxattr[2];
    uint64_t    export_lv1_resolve_entry[2];
    uint64_t    export_lv2_resolve_path[2];
    uint64_t    export_lookup_fid[2];
    uint64_t    export_update_files[2];
    uint64_t    export_update_blocks[2];
    uint64_t    export_stat[2];
    uint64_t    export_lookup[2];
    uint64_t    export_getattr[2];
    uint64_t    export_setattr[2];
    uint64_t    export_link[2];
    uint64_t    export_mknod[2];
    uint64_t    export_mkdir[2];
    uint64_t    export_unlink[2];
    uint64_t    export_rmdir[2];
    uint64_t    export_symlink[2];
    uint64_t    export_readlink[2];
    uint64_t    export_rename[2];
    uint64_t    export_read[3];
    uint64_t    export_read_block[2];
    uint64_t    export_write_block[2];
    uint64_t    export_setxattr[2];
    uint64_t    export_getxattr[2];
    uint64_t    export_removexattr[2];
    uint64_t    export_listxattr[2];
    uint64_t    export_readdir[2];
    uint64_t    lv2_cache_put[2];
    uint64_t    lv2_cache_get[2];
    uint64_t    lv2_cache_del[2];
    uint64_t    volume_balance[2];
    uint64_t    volume_distribute[2];
    uint64_t    volume_stat[2];
    uint64_t    mdir_open[2];
    uint64_t    mdir_close[2];
    uint64_t    mdir_read_attributes[2];
    uint64_t    mdir_write_attributes[2];
    uint64_t    mreg_open[2];
    uint64_t    mreg_close[2];
    uint64_t    mreg_read_attributes[2];
    uint64_t    mreg_write_attributes[2];
    uint64_t    mreg_read_dist[2];
    uint64_t    mreg_write_dist[2];
    uint64_t    mslnk_open[2];
    uint64_t    mslnk_close[2];
    uint64_t    mslnk_read_attributes[2];
    uint64_t    mslnk_write_attributes[2];
    uint64_t    mslnk_read_link[2];
    uint64_t    mslnk_write_link[2];
    uint64_t    get_mdirentry[2];
    uint64_t    put_mdirentry[2];
    uint64_t    del_mdirentry[2];
    uint64_t    list_mdirentries[2];
    
    uint64_t    gw_invalidate[2];
    uint64_t    gw_invalidate_all[2];
    uint64_t    gw_configuration[2];
    uint64_t    gw_poll[2];
    uint64_t    ep_configuration[2];
    uint64_t    ep_conf_gateway[2];
    uint64_t    ep_poll[2];

};

union epp_profiler_ret_t switch (epp_status_t status) {
    case EPP_SUCCESS:    epp_profiler_t profiler;
    case EPP_FAILURE:    int error;
    default:            void;
};

program EXPORTD_PROFILE_PROGRAM {
    version EXPORTD_PROFILE_VERSION {
        void
        EPP_NULL(void)          = 0;

        epp_profiler_ret_t
        EPP_GET_PROFILER(void)  = 1;

        epp_status_ret_t
        EPP_CLEAR(void)         = 2;

    }=1;
} = 0x20000005;
