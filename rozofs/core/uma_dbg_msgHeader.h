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
#ifndef UMA_DBG_MSGHEADER_H
#define UMA_DBG_MSGHEADER_H

#include <rozofs/common/types.h>

#include "ruc_common.h"

typedef struct uma_msgHeader_s {
  uint32_t       len;
  uint8_t        end;
  uint8_t        lupsId;
  uint8_t        precedence;
  uint8_t        filler;
} UMA_MSGHEADER_S;

#endif
