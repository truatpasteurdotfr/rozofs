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
#ifndef RUC_TRACE_API
#define RUC_TRACE_API

#include <stdint.h>

#include "ruc_common.h"

#define RUC_WARNING(p1) ruc_warning(__FILE__,__LINE__,(uint64_t)(long)p1)

uint32_t ruc_warning(char *source, uint32_t line, uint64_t errCode);
void
ruc_trace(char *appId, uint64_t par1, uint64_t par2, uint64_t par3, uint64_t par4);
void ruc_traceBufInit();
void ruc_printoff();
void ruc_printon();
void ruc_traceprint();
#endif
