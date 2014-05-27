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
#ifndef GEO_REPLICA_STR_H
#define GEO_REPLICA_STR_H
#include <rozofs/rozofs.h>
#include <stdint.h>

#define GEO_MAX_RECORDS 16  /**< max number of records in a syncho message */

 /**
 * file entry used for geo-relication
 */
 typedef struct _geo_fid_entry_t
 {
    fid_t fid;
    uint64_t off_start;
    uint64_t off_end;
    cid_t cid;  /**< cluster id 0 for non regular files */
    sid_t sids[ROZOFS_SAFE_MAX];    /**< sid of storage nodes target (regular file only)*/
    uint8_t layout;
} geo_fid_entry_t;

#endif
