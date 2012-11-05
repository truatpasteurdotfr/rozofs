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


#ifndef _SCLIENT_H
#define _SCLIENT_H

#include <uuid/uuid.h>

#include <rozofs/rozofs.h>

#include "rpcclt.h"

typedef struct sclient {
    char host[ROZOFS_HOSTNAME_MAX];
    //sid_t sid;
    uint32_t port;
    int status;
    rpcclt_t rpcclt;
} sclient_t;

int sclient_initialize(sclient_t * clt);

void sclient_release(sclient_t * clt);

int sclient_write(sclient_t * clt, sid_t sid, fid_t fid, tid_t tid, bid_t bid,
                     uint32_t nrb, const bin_t * bins);

int sclient_read(sclient_t * clt, sid_t sid, fid_t fid, tid_t tid, bid_t bid,
                    uint32_t nrb, bin_t * bins);

#endif
