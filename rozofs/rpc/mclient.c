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

#include <string.h>
#include <errno.h>

#include <rozofs/common/log.h>

#include "mproto.h"
#include "rpcclt.h"
#include "mclient.h"

int mclient_initialize(mclient_t * clt) {
    int status = -1;
    DEBUG_FUNCTION;

    clt->status = 0;

    if (rpcclt_initialize(&clt->rpcclt, clt->host, MONITOR_PROGRAM, MONITOR_VERSION, ROZOFS_RPC_BUFFER_SIZE, ROZOFS_RPC_BUFFER_SIZE, 0) != 0) {
        // storageclt_release can change errno
        int xerrno = errno;
        //storageclt_release(clt);
        clt->status = 0;
        errno = xerrno;
        goto out;
    }
    clt->status = 1;

    status = 0;
out:
    return status;
}

// XXX Useless
void mclient_release(mclient_t * clt) {
    DEBUG_FUNCTION;
    if (clt && clt->rpcclt.client)
        rpcclt_release(&clt->rpcclt);
}

int mclient_stat(mclient_t * clt, sstat_t * st) {
    int status = -1;
    mp_stat_ret_t *ret = 0;
    DEBUG_FUNCTION;

    if (!(clt->rpcclt.client) || !(ret = mp_stat_1(&clt->sid, clt->rpcclt.client))) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status != 0) {
        errno = ret->mp_stat_ret_t_u.error;
        goto out;
    }
    memcpy(st, &ret->mp_stat_ret_t_u.sstat, sizeof (sstat_t));

    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_mp_stat_ret_t, (char *) ret);
    return status;
}

int mclient_remove(mclient_t * clt, fid_t fid) {
    int status = -1;
    mp_status_ret_t *ret = 0;
    mp_remove_arg_t args;
    DEBUG_FUNCTION;

    args.sid = clt->sid;
    memcpy(args.fid, fid, sizeof (fid_t));
    if (!(clt->rpcclt.client) || !(ret = mp_remove_1(&args, clt->rpcclt.client))) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status != 0) {
        errno = ret->mp_status_ret_t_u.error;
        goto out;
    }
    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_mp_status_ret_t, (char *) ret);
    return status;
}

int mclient_ports(mclient_t * mclt, uint32_t * ports_p) {
    int status = -1;
    mp_ports_ret_t *ret = 0;
    DEBUG_FUNCTION;

    if (!(mclt->rpcclt.client) || !(ret = mp_ports_1(NULL,mclt->rpcclt.client))) {
        errno = EPROTO;
        goto out;
    }
    if (ret->status != 0) {
        errno = ret->mp_ports_ret_t_u.error;
        goto out;
    }
    memcpy(ports_p, &ret->mp_ports_ret_t_u.ports, STORAGE_NODE_PORTS_MAX * sizeof (uint32_t));

    status = 0;
out:
    if (ret)
        xdr_free((xdrproc_t) xdr_mp_ports_ret_t, (char *) ret);
    return status;
}
