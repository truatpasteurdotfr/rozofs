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

#include <string.h>
#include <errno.h>

#include <rozofs/common/log.h>

#include "mpproto.h"
#include "rpcclt.h"
#include "mpclient.h"

int mp_client_initialize(mp_client_t *clt) {
    int status = -1;
    DEBUG_FUNCTION;

    if (rpcclt_initialize(&clt->rpcclt, clt->host, ROZOFSMOUNT_PROFILE_PROGRAM,
            ROZOFSMOUNT_PROFILE_VERSION, 0, 0, clt->port) != 0) {
        goto out;
    }

    status = 0;
out:
    return status;
}

// XXX Useless
void mp_client_release(mp_client_t *clt) {
    DEBUG_FUNCTION;
    if (clt && clt->rpcclt.client)
        rpcclt_release(&clt->rpcclt);
}

int mp_client_get_profiler(mp_client_t *clt, mpp_profiler_t *p) {

    int status = -1;
    mpp_profiler_ret_t *ret = 0;

    if (!(clt->rpcclt.client) ||
            !(ret = mpp_get_profiler_1(0, clt->rpcclt.client))) {
        warning("rfsm_get_rozofrfsmount_profiler failed:\
                 no response from storage server (%s, %u)",
                 clt->host, clt->port);
        errno = EPROTO;
        goto out;
    }
    if (ret->status != 0) {
        errno = ret->mpp_profiler_ret_t_u.error;
        severe("rfsm_get_profiler failed: %s", strerror(errno));
        goto out;
    }
    memcpy(p, &ret->mpp_profiler_ret_t_u.profiler, sizeof(mpp_profiler_t));
    status = 0;
out:
    if (ret)
       xdr_free((xdrproc_t)xdr_mpp_profiler_ret_t, (char *) ret);
    return status;
}

int mp_client_clear(mp_client_t *clt) {
    int status = -1;
    mpp_status_ret_t *ret = 0;

    if (!(clt->rpcclt.client) || !(ret = mpp_clear_1(0, clt->rpcclt.client))) {
        warning("mp_client_clear failed:\
                 no response from storage server (%s, %u)",
                 clt->host, clt->port);
        errno = EPROTO;
        goto out;
    }
    if (ret->status != 0) {
        errno = ret->mpp_status_ret_t_u.error;
        severe("mp_client_clear failed: %s", strerror(errno));
        goto out;
    }
    status = 0;
out:
    if (ret)
       xdr_free((xdrproc_t)xdr_mpp_status_ret_t, (char *) ret);
    return status;
}
