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


#ifndef _EPCLIENT_H
#define _EPCLIENT_H

#include <uuid/uuid.h>

#include <rozofs/rozofs.h>

#include "epproto.h"
#include "rpcclt.h"

typedef struct ep_client {
    char host[ROZOFS_HOSTNAME_MAX];
    uint32_t port;
    rpcclt_t rpcclt;
} ep_client_t;

int ep_client_initialize(ep_client_t *clt);

void ep_client_release(ep_client_t *clt);

int ep_client_get_profiler(ep_client_t *clt, epp_profiler_t *p);

int ep_client_clear(ep_client_t *clt);

#endif
