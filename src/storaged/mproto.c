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

int storaged_update_device_info(storage_t * st) {
  time_t          t;

  /* 
  ** Let's allocate memory to cache device info
  */
  if (st->device_info_cache == NULL) {
  
    st->device_info_cache = xmalloc(sizeof(storage_device_info_cache_t));
    if (st->device_info_cache == NULL) {
      return ENOMEM;
    }
    st->device_info_cache->time   = 0;
    st->device_info_cache->nb_dev = 0;
  }

  /*
  ** Read statistics from disk if cache is out dated
  */
  t = time(NULL);
  if ((t-st->device_info_cache->time)>3) {
    st->device_info_cache->nb_dev = storage_read_device_status(st->root,st->device_info_cache->device); 
    st->device_info_cache->time   = t;
  }
  
  return 0;
}
void mp_stat_1_svc_nb(void * pt_req, rozorpc_srv_ctx_t *rozorpc_srv_ctx_p,
        void * pt_resp) {

    mp_stat_arg_t * args = (mp_stat_arg_t *) pt_req;
    mp_stat_ret_t * ret = (mp_stat_ret_t *) pt_resp;
    storage_t     * st = 0;
    uint64_t        ssize;
    uint64_t        sfree;
    int             device;

    DEBUG_FUNCTION;

    START_PROFILING(stat);

    ret->status = MP_FAILURE;

    if ((st = storaged_lookup(args->cid, args->sid)) == 0) {
        ret->mp_stat_ret_t_u.error = errno;
        goto out;
    }

    sfree = 0;
    ssize = 0;

    /* 
    ** Let's update device info
    */
    ret->mp_stat_ret_t_u.error = storaged_update_device_info(st);
    if (ret->mp_stat_ret_t_u.error != 0) goto out;
    

    for (device=0; device < st->device_info_cache->nb_dev; device++) {
      sfree += st->device_info_cache->device[device].free;
      ssize += st->device_info_cache->device[device].size;
    }  
    
    ret->mp_stat_ret_t_u.sstat.size = ssize;
    ret->mp_stat_ret_t_u.sstat.free = sfree;

    ret->status = MP_SUCCESS;

out:
    STOP_PROFILING(stat);
}

void mp_remove_1_svc_nb(void * pt_req, rozorpc_srv_ctx_t *rozorpc_srv_ctx_p,
        void * pt_resp) {

    mp_status_ret_t * ret = (mp_status_ret_t *) pt_resp;
    mp_remove_arg_t * args = (mp_remove_arg_t*) pt_req;
    storage_t *st = 0;

    DEBUG_FUNCTION;

    START_PROFILING(remove);

    ret->status = MP_FAILURE;

    if ((st = storaged_lookup(args->cid, args->sid)) == 0) {
        ret->mp_status_ret_t_u.error = errno;
        goto out;
    }

    if (storage_rm_file(st, (unsigned char *) args->fid) != 0) {
        ret->mp_status_ret_t_u.error = errno;
        goto out;
    }

    ret->status = MP_SUCCESS;

out:
    STOP_PROFILING(remove);
}

void mp_ports_1_svc_nb(void * pt_req, rozorpc_srv_ctx_t *rozorpc_srv_ctx_p,
        void * pt_resp) {

    mp_ports_ret_t * ret = (mp_ports_ret_t *) pt_resp;

    START_PROFILING(ports);

    ret->status = MP_SUCCESS;

    memcpy(&ret->mp_ports_ret_t_u.ports.io_addr, storaged_config.io_addr,
            sizeof(storaged_config.io_addr));
	    
    if (storaged_config.multiio == 0) {
      ret->mp_ports_ret_t_u.ports.mode = MP_SINGLE;
    }    
    else {
      ret->mp_ports_ret_t_u.ports.mode = MP_MULTIPLE;
    }
    
    STOP_PROFILING(ports);
}

void mp_list_bins_files_1_svc_nb(void * pt_req,
        rozorpc_srv_ctx_t *rozorpc_srv_ctx_p, void * pt_resp) {

    mp_list_bins_files_ret_t * ret = (mp_list_bins_files_ret_t *) pt_resp;
    mp_list_bins_files_arg_t * args = (mp_list_bins_files_arg_t*)  pt_req;

    storage_t *st = 0;

    ret->status = MP_FAILURE;

    START_PROFILING(list_bins_files);

    DEBUG_FUNCTION;

    if ((st = storaged_lookup(args->cid, args->sid)) == 0) {
        ret->mp_list_bins_files_ret_t_u.error = errno;
        goto out;
    }

    // It's necessary
    memset(ret, 0, sizeof(mp_list_bins_files_ret_t));

    if (storage_list_bins_files_to_rebuild(st, args->rebuild_sid,
            &args->device,
            &args->spare,
	    &args->slice,
            &args->cookie,
            (bins_file_rebuild_t **)
            & ret->mp_list_bins_files_ret_t_u.reply.children,
            (uint8_t *) & ret->mp_list_bins_files_ret_t_u.reply.eof) != 0) {
      ret->mp_list_bins_files_ret_t_u.error = errno;
      goto out;
    }

    ret->mp_list_bins_files_ret_t_u.reply.cookie = args->cookie;
    ret->mp_list_bins_files_ret_t_u.reply.spare = args->spare;    
    ret->mp_list_bins_files_ret_t_u.reply.device = args->device;
    ret->mp_list_bins_files_ret_t_u.reply.slice = args->slice;

    ret->status = MP_SUCCESS;

out:
    STOP_PROFILING(list_bins_files);
}
