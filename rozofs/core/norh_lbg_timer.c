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
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/times.h>
#include <sys/time.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_list.h"
#include "north_lbg_timer_api.h"
#include "north_lbg_timer.h"
#include "ppu_trace.h"

#define NORTH_LBG_TIMER_MAX_DATE 0xFFFFFFFF


/*
**   G L O B A L    D A T A
*/

/*
**  chartim variables (trace=false)
*/

north_lbg_tmr_var_t north_lbg_tmr={FALSE};

#define MILLISECLONG(time) (((unsigned long long)time.tv_sec * 1000000 + time.tv_usec)/1000)


/************************/
/* internal functions   */
/************************/

uint32_t north_lbg_time()
{
  struct timeval       timeDay;
  unsigned long long mylonglong;
  uint32_t  mylong;

  gettimeofday(&timeDay,(struct timezone *)0);
  mylonglong = MILLISECLONG(timeDay);
  /*
  ** put the limit to 2 minutes
  */
  mylong = (uint32_t) mylonglong;
  mylong &= NORTH_LBG_TIMER_MAX_DATE;

  return (uint32_t) mylong;
}

/*----------------------------------------------
**  north_lbg_tmr_periodic
**----------------------------------------------
**
**  It perform the processing of a limited number
**  of queued elements
**
**  IN : not significant
**
**  OUT : none
**
**-----------------------------------------------
*/
void north_lbg_tmr_processSlot(ruc_obj_desc_t* pTmrQueue) ;

void north_lbg_tmr_periodic(void *ns)
{

//  printf("north_lbg_tmr_periodic Ticker!\n");
  int i;

  for ( i = 0; i < NORTH_LBG_TMR_SLOT_MAX; i++)
  {
     north_lbg_tmr_processSlot(&north_lbg_tmr.queue[i]);
  }

}

/*----------------------------------------------
**  north_lbg_tmr_processSlot
**----------------------------------------------
**
**  It performs
**
**
**  IN :
**
**  OUT : none
**
**-----------------------------------------------
*/
void north_lbg_tmr_processSlot(ruc_obj_desc_t* pTmrQueue)
{
   int32_t credit;
   //int32_t delay_s;
   north_lbg_tmr_cell_t * p_refTim;
   uint32_t cur_date_s;
   uint8_t  expired = FALSE;

    /*
    ** processing credit and date initialization
    */
    credit     = north_lbg_tmr.credit;
    cur_date_s = north_lbg_time();

    /*
    ** process elements while there is still credit,
    ** and while time-out date is passed,
    ** and while there are still some elements
    */

    while (
           ((p_refTim=(north_lbg_tmr_cell_t *)ruc_objGetFirst(pTmrQueue)) !=
                    (north_lbg_tmr_cell_t *)NULL)
            && (credit > 0)
          ) {
        /*
        ** evaluate delay up to time out.
        ** if time out not yet passed, stop read queue
        */
	expired = FALSE;
	while (1)
	{
	  if (cur_date_s > p_refTim->date_s)
	  {
	    /*
	    ** it is possible that the counter has wrapped
	    */
	    if ((NORTH_LBG_TIMER_MAX_DATE - cur_date_s + p_refTim->date_s) < p_refTim->delay)
	    {
	      /*
	      ** nothing to do
	      */

	      break;
	    }
	    expired = TRUE;
	    break;
	  }
	  if (cur_date_s == p_refTim->date_s)
	  {
	    expired = TRUE;
	    break;
	  }
	  if (cur_date_s < p_refTim->date_s)
	  {
	    /*
	    ** no expiration but look at the delay request since
	    ** if somebody has changed the data we can be out of
	    ** sync
	    */
	    if ((p_refTim->date_s - cur_date_s) > p_refTim->delay)
	    {
	       /*
	       ** readjust the expiration date
	       */
	       p_refTim->date_s = cur_date_s + p_refTim->delay;
	    }
	  }
	  break;
	}
	if (!expired)
	{
	  /*
	  ** not expiration leaves the loop
	  */
	  return;
	}

        /*
        ** timer cell dequeing from its current queue
        */
        ruc_objRemove((ruc_obj_desc_t *)p_refTim);
	/*
	** call the time-out function
	*/
        (*(p_refTim->p_callBack))(p_refTim->cBParam);

        credit--;

    }

    return;
}



/************************/
/* APIS                 */
/************************/

/*
**----------------------------------------------
**  north_lbg_tmr_start
**----------------------------------------------
**
**  charging timer service starting request
**
**  IN : p_refTim   : reference of the timer cell to use
**       date_s     : requested time out date, in seconds
**       p_callBack : client call back to call at time out
**       cBParam    : client parameter to provide at time out
**
**  OUT : OK/NOK
**
**-----------------------------------------------
*/
uint32_t north_lbg_tmr_start (uint8_t   tmr_slot,
                        north_lbg_tmr_cell_t *p_refTim,
                        uint32_t date_s,
                        north_lbg_tmr_callBack_t p_callBack,
                        void *cBParam
			)
{
     uint32_t ret;

     if (tmr_slot >= NORTH_LBG_TMR_SLOT_MAX)
     {
        ERRLOG "north_lbg_tmr_start : slot out of range : %d ",tmr_slot ENDERRLOG
	return RUC_NOK;
     }
    /*
    ** timer cell dequeing from its current queue
    ** This dequeing is always attempted, even if not queued
    ** As the list primitive is protected, no initial check
    ** need to be done
    */
    ruc_objRemove((ruc_obj_desc_t *)p_refTim);

    /*
    ** initialize context
    */
    p_refTim->date_s = date_s + north_lbg_time();
    p_refTim->date_s &= NORTH_LBG_TIMER_MAX_DATE;
    p_refTim->delay = date_s;

    p_refTim->p_callBack = p_callBack;
    p_refTim->cBParam = cBParam;


    ret=ruc_objInsertTail(&north_lbg_tmr.queue[tmr_slot],(ruc_obj_desc_t*)p_refTim);
    if(ret!=RUC_OK){
        ERRLOG "Pb while inserting cell in queue, ret=%u", ret ENDERRLOG
        return(RUC_NOK);
    }
    return(RUC_OK);
}


/*
**----------------------------------------------
**  north_lbg_tmr_stop
**----------------------------------------------
**
**  charging timer service stoping request
**
**  IN : reference of the timer cell to stop
**
**  OUT : OK/NOK
**
**-----------------------------------------------
*/
uint32_t north_lbg_tmr_stop (north_lbg_tmr_cell_t *p_refTim){


    /*
    ** dequeue the timer cell
    */
    ruc_objRemove((ruc_obj_desc_t *)p_refTim);

    return(RUC_OK);
}

/*
**----------------------------------------------
**  north_lbg_tmr_init
**----------------------------------------------
**
**   charging timer service initialisation request
**
**  IN : period_ms : period between two queue sequence reading in ms
**       credit    : pdp credit processing number
**
**  OUT : OK/NOK
**
**-----------------------------------------------
*/
int north_lbg_tmr_init(uint32_t period_ms,
                    uint32_t credit)
{

    int i;

    /**************************/
    /* configuration variable */
    /* initialization         */
    /**************************/


    /*
    ** time between to look up to the charging timer queue
    */
    if (period_ms!=0){
        north_lbg_tmr.period_ms=period_ms;
    } else {
        ERRLOG "bad provided timer period (0 ms), I continue with 100 ms" ENDERRLOG
        north_lbg_tmr.period_ms=100;
    }

    /*
    ** Number of pdp context processed at each look up;
    */
    if (credit!=0){
        north_lbg_tmr.credit=credit;
    } else {
        ERRLOG "bad provided  credit (0), I continue with 1" ENDERRLOG
        north_lbg_tmr.credit=1;
    }

    /**************************/
    /* working variable       */
    /* initialization         */
    /**************************/


    /*
    **  queue initialization
    */
    for (i = 0; i < NORTH_LBG_TMR_SLOT_MAX; i++)
    {
      ruc_listHdrInit(&north_lbg_tmr.queue[i]);
    }

    /*
    ** charging timer periodic launching
    */
    north_lbg_tmr.p_periodic_timCell=ruc_timer_alloc(0,0);
    if (north_lbg_tmr.p_periodic_timCell == (struct timer_cell *)NULL){
        ERRLOG "No timer available for MS timer periodic" ENDERRLOG
        return(RUC_NOK);
    }
    ruc_periodic_timer_start(north_lbg_tmr.p_periodic_timCell,
	      (north_lbg_tmr.period_ms*TIMER_TICK_VALUE_100MS/100),
	      north_lbg_tmr_periodic,
	      0);

    return(RUC_OK);
}

