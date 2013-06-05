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
%#define GW_NAME_LEN  (ROZOFS_HOSTNAME_MAX/4)

typedef string          epgw_host_t<ROZOFS_PATH_MAX>;


 
enum gw_status_e {
    GW_FAILURE = 0,
    GW_SUCCESS = 1,
    GW_NOT_SYNCED = 2
};


struct gw_header_t {
  uint32_t export_id;
  uint32_t nb_gateways;
  uint32_t gateway_rank;
  uint32_t configuration_indice;
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

struct gw_host_conf_t  
{
  epgw_host_t   host;
};
  
struct gw_configuration_t {
  gw_header_t        hdr;
  epgw_host_t          exportd_host;
  uint16_t           exportd_port;
  uint16_t           gateway_port;
%//  uint32_t           eid[EXPGW_EID_MAX_IDX];  
  uint32_t           eid<>;  
  gw_host_conf_t     gateway_host<>;
%//  gw_host_conf_t     gateway_host[EXPGW_EXPGW_MAX_IDX];
} ; 
  
struct gw_ret_configuration_t
{
        gw_status_t ret;
        gw_configuration_t config;
};

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

        gw_status_t
        GW_POLL(gw_header_t)                                       = 4;

        gw_ret_configuration_t
        GW_GET_CONFIGURATION(gw_header_t)                          = 5;

    } = 1;
} = 0x20000009;
