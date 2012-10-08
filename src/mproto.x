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

%#include "rozofs.h"

typedef unsigned char mp_uuid_t[ROZOFS_UUID_SIZE];

enum mp_status_t {
    MP_SUCCESS = 0,
    MP_FAILURE = 1
};

union mp_status_ret_t switch (mp_status_t status) {
    case MP_FAILURE:    int error;
    default:            void;
};

struct mp_remove_arg_t {
    uint16_t    sid;
    mp_uuid_t   fid;
};

struct mp_sstat_t {
    uint64_t size;
    uint64_t free;
};

union mp_stat_ret_t switch (mp_status_t status) {
    case MP_SUCCESS:    mp_sstat_t  sstat;
    case MP_FAILURE:    int         error;
    default:            void;
};

union mp_ports_ret_t switch (mp_status_t status) {
    case MP_SUCCESS:    uint32_t     ports[STORAGE_NODE_PORTS_MAX];
    case MP_FAILURE:    int         error;
    default:            void;
};

program MONITOR_PROGRAM {
    version MONITOR_VERSION {
        void
        MP_NULL(void)                   = 0;

        mp_stat_ret_t
        MP_STAT(uint16_t)               = 1;

        mp_status_ret_t
        MP_REMOVE(mp_remove_arg_t)      = 2;

        mp_ports_ret_t
        MP_PORTS(void)                  = 3;

    }=1;
} = 0x20000003;

