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

#include "epproto.h"
#include "rpcclt.h"
#include "epclient.h"

int ep_client_initialize(ep_client_t *clt) {
    int status = -1;
    struct timeval tv = {clt->timeout, 0};
    DEBUG_FUNCTION;

    if (rpcclt_initialize(&clt->rpcclt, clt->host, EXPORTD_PROFILE_PROGRAM,
            EXPORTD_PROFILE_VERSION, 0, 0, clt->port, tv) != 0) {
        goto out;
    }

    status = 0;
out:
    return status;
}

// XXX Useless

void ep_client_release(ep_client_t *clt) {
    DEBUG_FUNCTION;
    if (clt && clt->rpcclt.client)
        rpcclt_release(&clt->rpcclt);
}

int ep_client_get_profiler(ep_client_t *clt, epp_profiler_t *p) {

    int status = -1;
    epp_profiler_ret_t *ret = 0;

    if (!(clt->rpcclt.client) ||
            !(ret = epp_get_profiler_1(0, clt->rpcclt.client))) {
        warning("rfsm_get_rozofrfsmount_profiler failed:\
                 no response from storage server (%s, %u)",
                clt->host, clt->port);
        errno = EPROTO;
        goto out;
    }
    if (ret->status != 0) {
        errno = ret->epp_profiler_ret_t_u.error;
        severe("rfsm_get_profiler failed: %s", strerror(errno));
        goto out;
    }
    memcpy(p, &ret->epp_profiler_ret_t_u.profiler, sizeof (epp_profiler_t));
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_epp_profiler_ret_t, (char *) ret);
    return status;
}

int ep_client_clear(ep_client_t *clt) {
    int status = -1;
    epp_status_ret_t *ret = 0;

    if (!(clt->rpcclt.client) || !(ret = epp_clear_1(0, clt->rpcclt.client))) {
        warning("ep_client_clear failed:\
                 no response from storage server (%s, %u)",
                clt->host, clt->port);
        errno = EPROTO;
        goto out;
    }
    if (ret->status != 0) {
        errno = ret->epp_status_ret_t_u.error;
        severe("ep_client_clear failed: %s", strerror(errno));
        goto out;
    }
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_epp_status_ret_t, (char *) ret);
    return status;
}
