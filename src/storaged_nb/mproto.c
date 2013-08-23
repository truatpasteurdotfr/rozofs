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

#include <limits.h>
#include <errno.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/rpc/mproto.h>
#include <rozofs/core/rozofs_rpc_non_blocking_generic_srv.h>

#include "storage.h"
#include "storaged.h"
#include "sconfig.h"

DECLARE_PROFILING(spp_profiler_t);

void mp_null_1_svc_nb(void * pt_req, 
                       rozorpc_srv_ctx_t  * rozorpc_srv_ctx_p,
                       void * pt_resp) { 
}
void mp_stat_1_svc_nb(void * pt_req, 
                      rozorpc_srv_ctx_t *rozorpc_srv_ctx_p, 
		      void * pt_resp) {
    mp_stat_arg_t * args = (mp_stat_arg_t * ) pt_req;    
    mp_stat_ret_t * ret  = (mp_stat_ret_t *)  pt_resp;
    storage_t *st = 0;
    sstat_t sstat;
    DEBUG_FUNCTION;

    START_PROFILING(stat);

    ret->status = MP_FAILURE;
    if ((st = storaged_lookup(args->cid, args->sid)) == 0) {
        ret->mp_stat_ret_t_u.error = errno;
        goto out;
    }
    if (storage_stat(st, &sstat) != 0) {
        ret->mp_stat_ret_t_u.error = errno;
        goto out;
    }
    ret->mp_stat_ret_t_u.sstat.size = sstat.size;
    ret->mp_stat_ret_t_u.sstat.free = sstat.free;
    ret->status = MP_SUCCESS;
out:
    STOP_PROFILING(stat);
}
void mp_remove_1_svc_nb(void * pt_req, 
                      rozorpc_srv_ctx_t *rozorpc_srv_ctx_p, 
		      void * pt_resp) {
    mp_status_ret_t * ret  = (mp_status_ret_t *) pt_resp;
    mp_remove_arg_t * args = (mp_remove_arg_t*)  pt_req;
    storage_t *st = 0;
    DEBUG_FUNCTION;

    START_PROFILING(remove);

    ret->status = MP_FAILURE;

    if ((st = storaged_lookup(args->cid, args->sid)) == 0) {
        ret->mp_status_ret_t_u.error = errno;
        goto out;
    }
    if (storage_rm_file(st, args->layout, (sid_t *) args->dist_set,
            (unsigned char *) args->fid) != 0) {
        ret->mp_status_ret_t_u.error = errno;
        goto out;
    }

    ret->status = MP_SUCCESS;
out:
    STOP_PROFILING(remove);
}
void mp_ports_1_svc_nb(void * pt_req, 
                       rozorpc_srv_ctx_t *rozorpc_srv_ctx_p, 
		       void * pt_resp) {
    mp_ports_ret_t * ret  = (mp_ports_ret_t *) pt_resp;

    START_PROFILING(ports);
    
    ret->status = MP_SUCCESS;
    memcpy(&ret->mp_ports_ret_t_u.io_addr, 
           storaged_config.io_addr,
           sizeof (storaged_config.io_addr));
	   
    STOP_PROFILING(ports);
}
void mp_list_bins_files_1_svc_nb(void * pt_req, 
                              rozorpc_srv_ctx_t *rozorpc_srv_ctx_p, 
	 	              void * pt_resp) {
    mp_list_bins_files_ret_t * ret  = (mp_list_bins_files_ret_t *) pt_resp;
    mp_list_bins_files_arg_t * args = (mp_list_bins_files_arg_t*)  pt_req;

    storage_t *st = 0;

    DEBUG_FUNCTION;

    if ((st = storaged_lookup(args->cid, args->sid)) == 0) {
        ret->mp_list_bins_files_ret_t_u.error = errno;
        return;
    }

    if (storage_list_bins_files_to_rebuild(st, args->rebuild_sid,
            &args->layout,
            (sid_t *) & args->dist_set,
            &args->spare,
            &args->cookie,
            (bins_file_rebuild_t **)
            & ret->mp_list_bins_files_ret_t_u.reply.children,
            (uint8_t *) & ret->mp_list_bins_files_ret_t_u.reply.eof) != 0) {
      ret->mp_list_bins_files_ret_t_u.error = errno;
      return;
    }

    ret->mp_list_bins_files_ret_t_u.reply.cookie = args->cookie;
    memcpy(&ret->mp_list_bins_files_ret_t_u.reply.dist_set, &args->dist_set,
            sizeof (sid_t) * ROZOFS_SAFE_MAX);
    ret->mp_list_bins_files_ret_t_u.reply.layout = args->layout;
    ret->mp_list_bins_files_ret_t_u.reply.spare = args->spare;

    ret->status = MP_SUCCESS;
}
