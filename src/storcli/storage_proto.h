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

#ifndef _STORAGE_PROTO_H
#define _STORAGE_PROTO_H

#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <rozofs/rozofs.h>
#include "config.h"

/**
* storage protocol opcodes
*/
typedef enum {
   SP_POLL_RQ = 0,
   SP_POLL_RSP ,
   SP_WRITE_RQ ,
   SP_WRITE_RSP ,
   SP_READ_RQ ,
   SP_READ_RSP ,
   SP_MAX_OPCODE 
} sproto_opcode_e;


typedef enum  {
	SPROTO_SUCCESS = 0,
	SPROTO_FAILURE = 1,
} sp_status_e ;
/**
* common status header
*/

typedef struct _sproto_status_hdr_t
{
    int status ;
    int errcode;
} sproto_status_hdr_t;

#endif
