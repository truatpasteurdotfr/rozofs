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
 
 #ifndef EXPGW_GWPROTOSVC_H
#define EXPGW_GWPROTOSVC_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

#include <stdio.h>
#include <stdlib.h>
#include <rozofs/rpc/gwproto.h>
#include <rozofs/rozofs.h>
#include <rozofs/core/rozofs_rpc_non_blocking_generic_srv.h>
#include <rozofs/rpc/rozofs_rpc_util.h>

void expgw_exportd_req_rcv_cbk(void *userRef,uint32_t  socket_ctx_idx, void *recv_buf);
/*
** Server side
*/
extern  void gw_null_1_nblocking_svc_cbk(void *, rozorpc_srv_ctx_t *);
extern  void gw_invalidate_sections_1_nblocking_svc_cbk(gw_invalidate_sections_t *, rozorpc_srv_ctx_t *);
extern  void gw_invalidate_all_1_nblocking_svc_cbk(gw_header_t *, rozorpc_srv_ctx_t *);
extern  void gw_configuration_1_nblocking_svc_cbk(gw_configuration_t *, rozorpc_srv_ctx_t *);
extern  void gw_poll_1_nblocking_svc_cbk(gw_header_t *, rozorpc_srv_ctx_t *);
/*
** Client side
*/
extern int 
gw_null_1_nblocking(void *argp, int lbg_id,void *ctx_p);
extern int 
gw_invalidate_sections_1_nblocking(gw_invalidate_sections_t *argp, int lbg_id,void *ctx_p);
extern int 
gw_invalidate_all_1_nblocking(gw_header_t *argp, int lbg_id,void *ctx_p);
extern int 
gw_configuration_1_nblocking(gw_configuration_t *argp, int lbg_id,void *ctx_p);
extern int 
gw_poll_1_nblocking(gw_header_t *argp, int lbg_id,void *ctx_p);


#ifdef __cplusplus
}
#endif /*__cplusplus */


#endif


