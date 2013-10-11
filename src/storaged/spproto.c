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
#include <time.h>

#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/spproto.h>
#include <rozofs/core/rozofs_rpc_non_blocking_generic_srv.h>
#include "spprotosvc.h"

DECLARE_PROFILING(spp_profiler_t);

void *spp_null_1_svc(void *args, struct svc_req *req) {
    DEBUG_FUNCTION;
    return 0;
}

spp_profiler_ret_t *spp_get_profiler_1_svc(void *args,struct svc_req *req) {
    static spp_profiler_ret_t ret;
    DEBUG_FUNCTION;

    gprofiler.now = time(0);
    memcpy(&ret.spp_profiler_ret_t_u.profiler, &gprofiler, sizeof(gprofiler));
    ret.status = SPP_SUCCESS;

    return &ret;
}

spp_status_ret_t *spp_clear_1_svc(void *args,struct svc_req *req) {
    static spp_status_ret_t ret;
    DEBUG_FUNCTION;

    CLEAR_PROBE(stat);
    CLEAR_PROBE(ports);
    CLEAR_PROBE(remove);
    CLEAR_PROBE(read);
    CLEAR_PROBE(write);
    CLEAR_PROBE(truncate);

    ret.status = SPP_SUCCESS;
    return &ret;
}

void spp_null_1_svc_nb(void * req, rozorpc_srv_ctx_t * rozorpc_srv_ctx_p, void * resp) {
}
void spp_get_profiler_1_svc_nb(void * req, rozorpc_srv_ctx_t * rozorpc_srv_ctx_p, void * resp) {
  spp_profiler_ret_t * ret = (spp_profiler_ret_t *) resp;
  
  gprofiler.now = time(0);
  memcpy(&ret->spp_profiler_ret_t_u.profiler, &gprofiler, sizeof(gprofiler));
  ret->status = SPP_SUCCESS;
}
void spp_clear_1_svc_nb(void * req, rozorpc_srv_ctx_t * rozorpc_srv_ctx_p, void * resp) {
  spp_status_ret_t * ret = (spp_status_ret_t*) resp;
  
  CLEAR_PROBE(stat);
  CLEAR_PROBE(ports);
  CLEAR_PROBE(remove);
  CLEAR_PROBE(read);
  CLEAR_PROBE(write);
  CLEAR_PROBE(truncate);

  ret->status = SPP_SUCCESS; 
}
