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

 
#ifndef SPPROTOSVC_H
#define SPROTOSVC_H

#include <stdint.h>

#include <config.h>
#include <rozofs/rozofs.h>
#include <rozofs/core/rozofs_rpc_non_blocking_generic_srv.h>
#include <rozofs/core/ruc_buffer_api.h>
#include <rozofs/rpc/rozofs_rpc_util.h>
#include <rozofs/common/profile.h>
#include <rozofs/rpc/mproto.h>
void spp_null_1_svc_nb(void * req, rozorpc_srv_ctx_t * rozorpc_srv_ctx_p, void * resp) ;
void spp_get_profiler_1_svc_nb(void * req, rozorpc_srv_ctx_t * rozorpc_srv_ctx_p, void * resp) ;
void spp_clear_1_svc_nb(void * req, rozorpc_srv_ctx_t * rozorpc_srv_ctx_p, void * resp) ;
#endif
