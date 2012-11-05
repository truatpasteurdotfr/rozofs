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

#include <limits.h>
#include <errno.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/epproto.h>

DECLARE_PROFILING(epp_profiler_t);

void *epp_null_1_svc(void *args, struct svc_req *req) {
    DEBUG_FUNCTION;
    return 0;
}

epp_profiler_ret_t *epp_get_profiler_1_svc(void * args,struct svc_req * req) {
    static epp_profiler_ret_t ret;
    DEBUG_FUNCTION;

    SET_PROBE_VALUE(now, time(0))
    memcpy(&ret.epp_profiler_ret_t_u.profiler, &gprofiler, sizeof(gprofiler));
    ret.status = EPP_SUCCESS;

    return &ret;
}

epp_status_ret_t *epp_clear_1_svc(void * args,struct svc_req * req) {
    static epp_status_ret_t ret;
    DEBUG_FUNCTION;

    CLEAR_PROBE(ep_mount);
    CLEAR_PROBE(ep_umount);
    CLEAR_PROBE(ep_statfs);
    CLEAR_PROBE(ep_lookup);
    CLEAR_PROBE(ep_getattr);
    CLEAR_PROBE(ep_setattr);
    CLEAR_PROBE(ep_readlink);
    CLEAR_PROBE(ep_mknod);
    CLEAR_PROBE(ep_mkdir);
    CLEAR_PROBE(ep_unlink);
    CLEAR_PROBE(ep_rmdir);
    CLEAR_PROBE(ep_symlink);
    CLEAR_PROBE(ep_rename);
    CLEAR_PROBE(ep_readdir);
    CLEAR_PROBE(ep_read_block);
    CLEAR_PROBE(ep_write_block);
    CLEAR_PROBE(ep_link);
    CLEAR_PROBE(export_lv1_resolve_entry);
    CLEAR_PROBE(export_lv2_resolve_path);
    CLEAR_PROBE(export_lookup_fid);
    CLEAR_PROBE(export_update_files);
    CLEAR_PROBE(export_update_blocks);
    CLEAR_PROBE(export_stat);
    CLEAR_PROBE(export_lookup);
    CLEAR_PROBE(export_getattr);
    CLEAR_PROBE(export_setattr);
    CLEAR_PROBE(export_link);
    CLEAR_PROBE(export_mknod);
    CLEAR_PROBE(export_mkdir);
    CLEAR_PROBE(export_unlink);
    CLEAR_PROBE(export_rmdir);
    CLEAR_PROBE(export_symlink);
    CLEAR_PROBE(export_readlink);
    CLEAR_PROBE(export_rename);
    CLEAR_PROBE(export_read);
    CLEAR_PROBE(export_read_block);
    CLEAR_PROBE(export_write_block);
    CLEAR_PROBE(export_readdir);
    CLEAR_PROBE(lv2_cache_put);
    CLEAR_PROBE(lv2_cache_get);
    CLEAR_PROBE(lv2_cache_del);
    CLEAR_PROBE(volume_balance);
    CLEAR_PROBE(volume_distribute);
    CLEAR_PROBE(volume_stat);
    CLEAR_PROBE(mdir_open);
    CLEAR_PROBE(mdir_close);
    CLEAR_PROBE(mdir_read_attributes);
    CLEAR_PROBE(mdir_write_attributes);
    CLEAR_PROBE(mreg_open);
    CLEAR_PROBE(mreg_close);
    CLEAR_PROBE(mreg_read_attributes);
    CLEAR_PROBE(mreg_write_attributes);
    CLEAR_PROBE(mreg_read_dist);
    CLEAR_PROBE(mreg_write_dist);
    CLEAR_PROBE(mslnk_open);
    CLEAR_PROBE(mslnk_close);
    CLEAR_PROBE(mdir_close);
    CLEAR_PROBE(mslnk_read_attributes);
    CLEAR_PROBE(mslnk_read_link);
    CLEAR_PROBE(mslnk_write_link);

    ret.status = EPP_SUCCESS;

    return &ret;
}
