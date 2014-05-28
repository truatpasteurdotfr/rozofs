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
#ifndef GEO_CTX_TMR_H
#define GEO_CTX_TMR_H

#include <rozofs/core/ruc_common.h>
#include <rozofs/core/ruc_list.h>
#include <rozofs/core/ruc_timer_api.h>

/*
** there is one timer slot per timr type
*/
#define  GEO_CTX_TMR_SLOT0         0
#define  GEO_CTX_TMR_SLOT1         1
#define  GEO_CTX_TMR_SLOT2         2
#define GEO_CTX_TMR_SLOT_MAX       3

typedef struct _geo_ctx_tmr_var_t {
 /* configuration variables */
    uint32_t         trace;
    uint32_t         period_ms;                      /* period between two lookup */
                                                   /* to the to queue           */
    uint32_t         credit;                         /* nb of element processed   */
                                                   /* at each look up           */             
 /* working variables */
    ruc_obj_desc_t queue[GEO_CTX_TMR_SLOT_MAX];	   /* queue                     */
    struct timer_cell * p_periodic_timCell;        /* periodic timer cell       */	 
} geo_ctx_tmr_var_t;


/*
**  prototypes (private)
*/

extern void geo_ctx_tmr_periodic(void *ns);

extern geo_ctx_tmr_var_t geo_ctx_tmr;


         /*----------------------*/
         /* charging timer types  */
         /*----------------------*/

typedef void (*geo_ctx_tmr_callBack_t)(void*) ; /* call back type */


         /*----------------------*/
         /* Timer cell structure */
         /*----------------------*/

typedef struct _geo_ctx_tmr_cell_t {
    ruc_obj_desc_t         listHead;    /* header used by list service for queing */
    uint32_t                 date_s;      /* time out date in system milliseconds */
    uint32_t                 delay;      /* delay requested in ms      */
    geo_ctx_tmr_callBack_t p_callBack;  /* call back to be used at time out */
    void                 *cBParam;     /* parameter to be provided at time out */
} geo_ctx_tmr_cell_t;

#define geo_ctx_tmr_CELL_LGTH		sizeof (struct geo_ctx_tmr_cell_t)


/*--------------------------------------------------------------------------*/


    /* Prepaid Timer management APIs */


/*
**  IN : tmr_slot : index of the timer slot.
**       p_refTim   : reference of the timer cell to use
**       date_s     : requested time out date, in seconds
**       p_callBack : client call back to call at time out
**       cBParam    : client parameter to provide at time out
**
**  OUT : OK/NOK
*/
extern uint32_t geo_ctx_tmr_start (uint8_t tmr_slot,
                               geo_ctx_tmr_cell_t *p_refTim, 
                               uint32_t date_s,
                               geo_ctx_tmr_callBack_t p_callBack,
                               void *cBParam);

/*
**  IN : reference of the timer cell to stop
**
**  OUT : OK/NOK
*/
 uint32_t geo_ctx_tmr_stop (geo_ctx_tmr_cell_t *p_refTim);


/*
**  IN : period_ms : period between two queue sequence reading in ms
**       credit    : pdp credit processing number
**
**  OUT : OK/NOK
*/

 int geo_ctx_tmr_init (uint32_t period_ms,uint32_t credit) ;


#endif
