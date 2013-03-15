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


#ifndef _MPCLIENT_H
#define _MPCLIENT_H

#include <uuid/uuid.h>

#include <rozofs/rozofs.h>

#include "mpproto.h"
#include "rpcclt.h"

typedef struct mp_client {
    char host[ROZOFS_HOSTNAME_MAX];
    uint32_t port;
    rpcclt_t rpcclt;
} mp_client_t;

int mp_client_initialize(mp_client_t *clt, struct timeval timeout);

void mp_client_release(mp_client_t *clt);

int mp_client_get_profiler(mp_client_t *clt, mpp_profiler_t *p);

int mp_client_clear(mp_client_t *clt);

#endif
