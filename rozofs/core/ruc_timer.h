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

#ifndef ASIG_SRV_TICK_EVT_H
#define ASIG_SRV_TICK_EVT_H

#include <rozofs/common/types.h>

#include "ruc_timer_api.h"

/*******************************************************************/
/*     TICK event mgt STRUCTURE                                    */
/*******************************************************************/


typedef struct _TICK_TAB_DESCRIPTOR_S {
    uint32_t   			reading_index;
    uint32_t   			writing_index;

} TICK_TAB_DESCRIPTOR_S;


/*
 *------------------------------------------------------------------
 *
 * MACROS:
 *
 *------------------------------------------------------------------
 */

/* index tick incrementation after a writing */
/* ****************************************** */

#define TICK_UPDATE_WRITING_INDEX(rcvTick) \
	(rcvTick.writing_index)++


/* index tick incrementation after a reading                       */
/* **************************************************************** */

#define TICK_UPDATE_READING_INDEX(rcvTick) \
	(rcvTick.reading_index)++


/* test if we hyave received tick                                   */
/* **************************************************************** */

#define TICK_RCV_NO_EMPTY(rcvTick) \
	(rcvTick.reading_index != rcvTick.writing_index)


#endif

/*------------------------------- End Of File---------------------------------*/
