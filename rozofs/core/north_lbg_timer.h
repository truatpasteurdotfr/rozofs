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
#ifndef north_lbg_tmr_H
#define north_lbg_tmr_H

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_list.h"
#include "ruc_timer_api.h"
#include "north_lbg_timer_api.h"

/*
**  Sgsn Include
*/



/****************************/
/* charging timer variables  */
/****************************/

typedef struct _north_lbg_tmr_var_t {
 /* configuration variables */
    uint32_t         trace;
    uint32_t         period_ms;                      /* period between two lookup */
                                                   /* to the to queue           */
    uint32_t         credit;                         /* nb of element processed   */
                                                   /* at each look up           */
 /* working variables */
    ruc_obj_desc_t queue[NORTH_LBG_TMR_SLOT_MAX];	   /* queue                     */
    struct timer_cell * p_periodic_timCell;        /* periodic timer cell       */
} north_lbg_tmr_var_t;



/*
**  prototypes (private)
*/

extern void north_lbg_tmr_periodic(void*);

extern north_lbg_tmr_var_t north_lbg_tmr;

#endif
