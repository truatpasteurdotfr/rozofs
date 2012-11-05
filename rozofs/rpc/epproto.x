/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

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

enum epp_status_t {
    EPP_SUCCESS = 0,
    EPP_FAILURE = 1
};

union epp_status_ret_t switch (epp_status_t status) {
    case EPP_FAILURE:    int error;
    default:            void;
};


struct epp_profiler_t {
    uint64_t    uptime;
    uint64_t    now;
    uint8_t     vers[20];
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
