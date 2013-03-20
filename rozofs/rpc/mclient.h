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


#ifndef _MCLIENT_H
#define _MCLIENT_H

#include <uuid/uuid.h>

#include <rozofs/rozofs.h>

#include "rpcclt.h"

typedef struct mclient {
    char host[ROZOFS_HOSTNAME_MAX];
    cid_t cid;
    sid_t sid;
    int status;
    rpcclt_t rpcclt;
} mclient_t;

int mclient_initialize(mclient_t *clt, struct timeval timeout);

void mclient_release(mclient_t *clt);

int mclient_stat(mclient_t *clt, sstat_t *st);

int mclient_remove(mclient_t * clt, uint8_t layout, 
        sid_t dist_set[ROZOFS_SAFE_MAX], fid_t fid);

int mclient_ports(mclient_t *mclt, uint32_t *ports_p);

#endif
