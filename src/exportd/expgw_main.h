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
 
 #ifndef EXPGW_MAIN_H
#define EXPGW_MAIN_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/


#include "expgw_fid_cache.h"
#include "expgw_attr_cache.h"
/**
*  Init of the data structure used for the non blocking entity

  @retval 0 on success
  @retval -1 on error
*/
int expgw_non_blocking_init(uint16_t dbg_port, uint16_t expgw_instance);




#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif
