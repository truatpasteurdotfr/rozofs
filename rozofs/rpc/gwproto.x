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


 
enum gw_status_e {
    GW_FAILURE = 0,
    GW_SUCCESS = 1
};


struct gw_header_t {
  uint32_t export_id;
  uint32_t nb_srv;
  uint32_t srv_rank;
};


union gw_status_t switch (gw_status_e status) {
    case GW_FAILURE:    int error;
    default:            void;
};

  
struct gw_dirty_section_t {
  uint32_t   absolute_idx;
  uint32_t   section_sz;
  uint8_t    bitmap<>;
};
 

struct gw_invalidate_sections_t {
  gw_header_t        hdr;
  gw_dirty_section_t section<>;
};  
  
  
struct gw_configuration_t {
  gw_header_t        hdr;
  uint32_t           ipAddr;
  uint16_t           port;
  uint32_t           eid<>;  
} ; 
  
program GW_PROGRAM {
    version GW_VERSION {
    
        void
        GW_NULL(void)                                              = 0;

        gw_status_t
        GW_INVALIDATE_SECTIONS(gw_invalidate_sections_t)           = 1;

        gw_status_t
        GW_INVALIDATE_ALL(gw_header_t)                             = 2;

        gw_status_t
        GW_CONFIGURATION(gw_configuration_t)                       = 3;

    } = 1;
} = 0x20000009;
