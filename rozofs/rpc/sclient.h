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

/**
 *  Header structure for one projection
 */
typedef union {
    uint64_t u64[2];

    struct {
        uint64_t timestamp : 64; ///<  time stamp. (not used yet)
        uint64_t effective_length : 16; ///<  effective length of the rebuilt block size: MAX is 64K.
        uint64_t projection_id : 8; ///<  index of the projection -> needed to find out angles/sizes: MAX is 255.
        uint64_t version : 8; ///<  version of rozofs. (not used yet)
        uint64_t filler : 32; ///<  for future usage.
    } s;
} rozofs_stor_bins_hdr_t;

int sclient_initialize(sclient_t * clt);

void sclient_release(sclient_t * clt);

int sclient_write(sclient_t * clt, cid_t cid, sid_t sid, uint8_t layout,
        uint8_t spare, sid_t dist_set[ROZOFS_SAFE_MAX], fid_t fid, tid_t proj_id,
        bid_t bid, uint32_t nb_proj, const bin_t * bins);

int sclient_read(sclient_t * clt, cid_t cid, sid_t sid, uint8_t layout,
        uint8_t spare, sid_t dist_set[ROZOFS_SAFE_MAX], fid_t fid, tid_t proj_id,
        bid_t bid, uint32_t nb_proj, bin_t * bins);

#endif
