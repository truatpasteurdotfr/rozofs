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

#ifndef AF_UNIX_TEST_H
#define AF_UNIX_TEST_H

#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <errno.h>
#include <stdarg.h>
#include <unistd.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "af_unix_socket_generic.h"

/**
* header use for af_unix test socket
*/

#define ROZOFS_TEST_SND_REQ 10
#define ROZOFS_TEST_SND_CNF 11

typedef struct _af_unix_test_hdr_t
{
  uint32_t opcode;
  uint32_t msg_len;
  uint32_t  transaction_id;
  void     *bufref_origin;
  void     *socket_ref;     /**< reference of the socket on which the request has been received -> used by server */
} af_unix_test_hdr_t;

/*
** structure of a message associated with a WQE
*/
typedef struct _af_unix_wqe_payload_t
{
  void     *buf_req_ref;   /**< reference of the ruc_buffer containing the request  */
  void     *buf_rsp_ref;   /**< reference of the ruc_buffer containing the response */
} af_unix_wqe_payload_t;


#define TEST_DISK_READ  0
#define TEST_DISK_WRITE 1

#define DISK_READ_WRITE_MAX_SIZE   (1024*256)
#define DISK_MAX_BUFFER            32

typedef struct _test_disk_req_t
{
    char     pathname[1024];  /**< patbname and file name  */
    int      fd;          /**< file descriptor if known or -1 if unknown  */
    int      count;       /**< number of bytes to read                 */
    off_t    offset;      /**< offset within the file                  */

} test_disk_req_t;



typedef struct _test_disk_rsp_t
{
    size_t      count;       /**< number of bytes read                */
    int         status;      /**< 0 -> OK , -1 error                  */
    int         errno_val;      /**< errno value                      */

} test_disk_rsp_t;
#endif
