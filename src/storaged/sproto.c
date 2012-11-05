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
#include <rozofs/rpc/spproto.h>
#include <rozofs/rpc/sproto.h>

#include "storage.h"
#include "storaged.h"

DECLARE_PROFILING(spp_profiler_t);

void *sp_null_1_svc(void *args, struct svc_req *req) {
    DEBUG_FUNCTION;
    return 0;
}

sp_status_ret_t *sp_write_1_svc(sp_write_arg_t * args, struct svc_req * req) {
    static sp_status_ret_t ret;
    storage_t *st = 0;
    DEBUG_FUNCTION;

    START_PROFILING_IO(write,
            args->nrb * rozofs_psizes[args->tid] * sizeof (bin_t));

    ret.status = SP_FAILURE;
    if ((st = storaged_lookup(args->sid)) == 0) {
        ret.sp_status_ret_t_u.error = errno;
        goto out;
    }
    if (storage_write
        (st, args->fid, args->tid, args->bid, args->nrb, args->bins.bins_len,
         (bin_t *) args->bins.bins_val) != 0) {
        ret.sp_status_ret_t_u.error = errno;
        goto out;
    }
    ret.status = SP_SUCCESS;
out:
    STOP_PROFILING(write);
    return &ret;
}

sp_read_ret_t *sp_read_1_svc(sp_read_arg_t * args, struct svc_req * req) {
    static sp_read_ret_t ret;
    uint32_t psize;
    storage_t *st = 0;
    DEBUG_FUNCTION;

    START_PROFILING_IO(read,
            args->nrb * rozofs_psizes[args->tid] * sizeof (bin_t));

    xdr_free((xdrproc_t) xdr_sp_read_ret_t, (char *) &ret);
    ret.status = SP_FAILURE;

    if ((st = storaged_lookup(args->sid)) == 0) {
        ret.sp_read_ret_t_u.error = errno;
        goto out;
    }
    psize = rozofs_psizes[args->tid];
    ret.sp_read_ret_t_u.bins.bins_len = args->nrb * psize * sizeof (bin_t);
    ret.sp_read_ret_t_u.bins.bins_val =
        (char *) xmalloc(args->nrb * psize * sizeof (bin_t));
    if (storage_read
        (st, args->fid, args->tid, args->bid, args->nrb,
         (bin_t *) ret.sp_read_ret_t_u.bins.bins_val) != 0) {
        ret.sp_read_ret_t_u.error = errno;
        goto out;
    }
    ret.status = SP_SUCCESS;

out:
    STOP_PROFILING(read);
    return &ret;
}

sp_status_ret_t *sp_truncate_1_svc(sp_truncate_arg_t * args,
                                   struct svc_req * req) {
    static sp_status_ret_t ret;
    storage_t *st = 0;
    DEBUG_FUNCTION;

    ret.status = SP_FAILURE;
    if ((st = storaged_lookup(args->sid)) == 0) {
        ret.sp_status_ret_t_u.error = errno;
        goto out;
    }
    if (storage_truncate(st, args->fid, args->tid, args->bid) != 0) {
        ret.sp_status_ret_t_u.error = errno;
        goto out;
    }
    ret.status = SP_SUCCESS;
out:
    return &ret;
}
