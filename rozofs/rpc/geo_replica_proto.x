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

%#include <rozofs/rozofs.h>


enum geo_status_t {
    GEO_SUCCESS = 0,
    GEO_FAILURE = 1
};


struct geo_sync_req_arg_t {
    uint16_t    eid;
    uint16_t    site_id;
    uint32_t     local_ref;           
};

struct geo_sync_data_ret_t
{
   uint16_t    eid;
   uint16_t    site_id;
   uint64_t    file_idx;
   uint32_t local_ref;
   uint32_t remote_ref;
   uint32_t last;
   uint32_t first_record;
   uint32_t nb_records;   
   opaque  data<>;
};

union geo_sync_req_ret_t switch (geo_status_t status) {
    case GEO_SUCCESS:   struct geo_sync_data_ret_t  data;
    case GEO_FAILURE:    int     error;
    default:            void;
};


struct geo_sync_get_next_req_arg_t
{
   uint16_t    eid;
   uint16_t    site_id;
   uint64_t    file_idx;
   uint32_t local_ref;
   uint32_t remote_ref;
   uint32_t next_record;
   uint64_t status_bitmap;
};



struct geo_sync_delete_req_arg_t
{
   uint16_t    eid;
   uint16_t    site_id;
   uint64_t    file_idx;
   uint32_t local_ref;
   uint32_t remote_ref;
};

struct geo_sync_close_req_arg_t
{
   uint16_t    eid;
   uint16_t    site_id;
   uint64_t    file_idx;
   uint32_t local_ref;
   uint32_t remote_ref;
   uint64_t status_bitmap;
};


union geo_status_ret_t switch (geo_status_t status) {
    case GEO_FAILURE:    int error;
    default:            void;
};

program GEO_PROGRAM {
    version GEO_VERSION {
        void
        GEO_NULL(void)                   = 0;

        geo_sync_req_ret_t
        GEO_SYNC_REQ(geo_sync_req_arg_t)        = 1;

        geo_sync_req_ret_t
        GEO_SYNC_GET_NEXT_REQ(geo_sync_get_next_req_arg_t)          = 2;

        geo_status_ret_t
        GEO_SYNC_DELETE_REQ(geo_sync_delete_req_arg_t)  = 3;

        geo_status_ret_t
        GEO_SYNC_CLOSE_REQ(geo_sync_close_req_arg_t)  = 4;

    }=1;
} = 0x20000010;

