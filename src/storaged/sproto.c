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
#include <rozofs/rozofs_srv.h>
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
    // Variable to be used in a later version.
    uint8_t version = 0;

    DEBUG_FUNCTION;

    START_PROFILING_IO(write,
            args->nb_proj * rozofs_get_psizes(args->layout, args->proj_id)
            * sizeof (bin_t));

    ret.status = SP_FAILURE;

    // Get the root path for this SID
    if ((st = storaged_lookup(args->sid)) == 0) {
        ret.sp_status_ret_t_u.error = errno;
        goto out;
    }

    // Write projections
    if (storage_write(st, args->layout, args->dist_set, args->spare, args->fid,
            args->bid, args->nb_proj, version,
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
    uint16_t psize = 0;
    storage_t *st = 0;

    DEBUG_FUNCTION;

    START_PROFILING_IO(read,
            args->nb_proj * rozofs_get_psizes(args->layout, args->proj_id)
            * sizeof (bin_t));

    xdr_free((xdrproc_t) xdr_sp_read_ret_t, (char *) &ret);

    ret.status = SP_FAILURE;

    // Get the root path for this SID
    if ((st = storaged_lookup(args->sid)) == 0) {
        ret.sp_read_ret_t_u.error = errno;
        goto out;
    }

    psize = rozofs_get_max_psize(args->layout);

    // Allocate memory
    ret.sp_read_ret_t_u.bins.bins_val =
            (char *) xmalloc(args->nb_proj * (psize * sizeof (bin_t) +
            sizeof (rozofs_stor_bins_hdr_t)));

    // Read projections
    if (storage_read(st, args->layout, args->dist_set, args->spare, args->fid,
            args->bid, args->nb_proj,
            (bin_t *) ret.sp_read_ret_t_u.bins.bins_val,
            (size_t *) &ret.sp_read_ret_t_u.bins.bins_len) != 0) {
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

    // Get the root path for this SID
    if ((st = storaged_lookup(args->sid)) == 0) {
        ret.sp_status_ret_t_u.error = errno;
        goto out;
    }

    // Truncate bins file
    if (storage_truncate(st, args->layout, args->dist_set, args->spare,
            args->fid, args->proj_id, args->bid) != 0) {
        ret.sp_status_ret_t_u.error = errno;
        goto out;
    }

    ret.status = SP_SUCCESS;
out:
    return &ret;
}
