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

#include "ruc_common.h"
#include <rozofs/common/types.h>
#include <netinet/in.h>
#include "ruc_trace_api.h"

#include <rozofs/common/log.h>

#include "ruc_trace_api.h"
#include "ruc_common.h"
#include "uma_dbg_api.h"
#include "ruc_sockCtl_api.h"
#include "ruc_tcp_client_api.h"
#include "ruc_tcpServer_api.h"
#include "af_unix_socket_generic_api.h"

int monitoring_initialize(uint32_t test, uint16_t port) {
    int status = -1;
    uint32_t mx_tcp_client = 10;
    uint32_t mx_tcp_server = 10;
    uint32_t mx_tcp_server_cnx = 10;
    uint32_t mx_af_unix_ctx = 2;

    /*
     ** trace buffer initialization
     */
    ruc_traceBufInit();

    /*
     ** initialize the socket controller:
     **   4 connections per Relc and 32
     **   for: NPS, Timer, Debug, etc...
     */
    if (ruc_sockctl_init(100) != 0) {
        severe(" socket controller init failed");
        goto out;
    }

    /*
     **  Timer management init
     */
    ruc_timer_moduleInit(FALSE);

    while (1) {
        /*
         **--------------------------------------
         **  configure the number of TCP connection
         **  supported
         **--------------------------------------
         **
         */
        if (uma_tcp_init(mx_tcp_client + mx_tcp_server + mx_tcp_server_cnx) != 0)
            break;

        /*
         **--------------------------------------
         **  configure the number of TCP server
         **  context supported
         **--------------------------------------
         **
         */
        if (ruc_tcp_server_init(mx_tcp_server) != 0)
            break;

        /*
         **--------------------------------------
         **  configure the number of TCP client
         **  context supported
         **--------------------------------------
         **
         */
        if (ruc_tcp_client_init(mx_tcp_client) != 0)
            break;

        /*
         **--------------------------------------
         **  configure the number of AF_UNIX
         **  context supported
         **--------------------------------------
         **
         */
        if (af_unix_module_init(mx_af_unix_ctx, 32, 1024, 32, 1024) != 0)
            break;
    }

    /*
     **--------------------------------------
     **   D E B U G   M O D U L E
     **--------------------------------------
     */
    uma_dbg_init(10, INADDR_ANY, port);
    status = 0;
out:
    return status;
}
