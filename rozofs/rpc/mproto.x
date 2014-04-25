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

%#include <rozofs/rozofs.h>

typedef uint32_t mp_uuid_t[ROZOFS_UUID_SIZE_NET];

enum mp_status_t {
    MP_SUCCESS = 0,
    MP_FAILURE = 1
};

union mp_status_ret_t switch (mp_status_t status) {
    case MP_FAILURE:    int error;
    default:            void;
};

struct mp_remove_arg_t {
    uint16_t    cid;
    uint8_t     sid;
    uint8_t     layout;
    uint32_t    dist_set[ROZOFS_SAFE_MAX_NET];
    mp_uuid_t   fid;
};

struct mp_stat_arg_t {
    uint16_t    cid;
    uint8_t     sid;
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

struct mp_io_address_t {
  uint32_t ipv4;
  uint32_t port;
};

union mp_ports_ret_t switch (mp_status_t status) {
    case MP_SUCCESS:    struct mp_io_address_t  io_addr[STORAGE_NODE_PORTS_MAX];
    case MP_FAILURE:    int         error;
    default:            void;
};

typedef struct mp_child_t *mp_children_t;

struct mp_child_t { 
    mp_uuid_t       fid;
    uint8_t         layout;
    uint8_t         dist_set[ROZOFS_SAFE_MAX];    
    mp_children_t   next;
};

struct bins_files_list_t {
    mp_children_t   children;
    uint8_t         eof;
    uint8_t         device;    
    uint16_t        slice;
    uint8_t         spare;
    uint64_t        cookie;
};

union mp_list_bins_files_ret_t switch (mp_status_t status) {
    case MP_SUCCESS:    bins_files_list_t       reply;
    case MP_FAILURE:    int                     error;
    default:            void;
};

struct mp_list_bins_files_arg_t {
    uint16_t   cid;
    uint8_t    sid;
    uint8_t    rebuild_sid;
    uint8_t    device;
    uint8_t    spare;
    uint16_t   slice;
    uint64_t   cookie;
};

program MONITOR_PROGRAM {
    version MONITOR_VERSION {
        void
        MP_NULL(void)                                   = 0;

        mp_stat_ret_t
        MP_STAT(mp_stat_arg_t)                          = 1;

        mp_status_ret_t
        MP_REMOVE(mp_remove_arg_t)                      = 2;

        mp_ports_ret_t
        MP_PORTS(void)                                  = 3;

        mp_list_bins_files_ret_t
        MP_LIST_BINS_FILES(mp_list_bins_files_arg_t)    = 4;

    }=1;
} = 0x20000003;
