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


#ifndef _SMCLIENT_H
#define _SMCLIENT_H

#include <uuid/uuid.h>

#include <rozofs/rozofs.h>

#include "spproto.h"
#include "rpcclt.h"

typedef struct sp_client {
    char host[ROZOFS_HOSTNAME_MAX];
    uint32_t port;
    rpcclt_t rpcclt;
} sp_client_t;

int sp_client_initialize(sp_client_t *clt);

void sp_client_release(sp_client_t *clt);

int sp_client_get_profiler(sp_client_t *clt, spp_profiler_t *sm);

int sp_client_clear(sp_client_t *clt);

#endif
