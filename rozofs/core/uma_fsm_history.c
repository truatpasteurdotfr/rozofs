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
#define UMA_FSM_HISTORY_C
/*
  _____________________________________________
  HISTORY OF AN FSM
  _____________________________________________
*/


#include <rozofs/common/types.h>

#include "uma_fsm_framework.h"

#define UMA_FSM_NEXT_IDX(x) x++;if (x>=UMA_FSM_MAX_HISTORY) x=0
static char formatBuff[2048];

/*__________________________________________________________________________
  Initialize the history part of the FSM
  ==========================================================================
  PARAMETERS:
  . pfsm : the address of the FSM
  RETURN: none
  ==========================================================================*/
void uma_fsm_history_init(uma_fsm_t * pfsm)
{
  int i;

  for (i = 0; i < UMA_FSM_MAX_HISTORY; i++)
  {
     pfsm->histRec[i].word[0] = 0;
     pfsm->histRec[i].word[1] = 0;
  }
  pfsm->curHistIdx = 0;

}
/*__________________________________________________________________________
  Trace a FSM state change and all its flags by this time
  ==========================================================================
  PARAMETERS:
  . new : whether a new record is to be created
  . objPtr : the object owning the FSM
  . pfsm   : the address of the FSM
  RETURN: none
  ==========================================================================*/
void uma_fsm_set_trace (uma_fsm_t * pfsm, uint8_t status) {
  if (status == FALSE) pfsm->trace = FALSE;
  else                 pfsm->trace = TRUE;
}
/*__________________________________________________________________________
  Trace a FSM state change and all its flags by this time
  ==========================================================================
  PARAMETERS:
  . new : whether a new record is to be created
  . objPtr : the object owning the FSM
  . pfsm   : the address of the FSM
  RETURN: none
  ==========================================================================*/
void uma_fsm_trace (uint8_t new, void * objPtr, uma_fsm_t * pfsm) {
  char  * pt;
  uint8_t historyIdx = pfsm->curHistIdx;

  /* Compute the next index of the trace record and store it */
  if (new) {
    UMA_FSM_NEXT_IDX(historyIdx);
    pfsm->curHistIdx = historyIdx;
  }

  /* Store the current FSM state & the current bit mask */
  pfsm->histRec[historyIdx].word[0] = pfsm->pflags[0];
  pfsm->histRec[historyIdx].word[1] = (pfsm->fsm_state & 0xFF)|(pfsm->pflags[1] & 0xFFFFFF00);

  if (pfsm->trace == TRUE) {
    if (pfsm->formatHist != NULL) {
      pt = formatBuff;
      pt = (pfsm->formatHist)(pt,new,pfsm->histRec[historyIdx].word[0], pfsm->histRec[historyIdx].word[1]);
      printf("%s\n", formatBuff);
    }
    else if (pfsm->printHistRec != NULL) {
      (pfsm->printHistRec) (new,pfsm->histRec[historyIdx].word[0], pfsm->histRec[historyIdx].word[1]);
    }
  }
}
/*__________________________________________________________________________
  Display the whole history of an FSM
  ==========================================================================
  PARAMETERS:
  . index : the index of the automaton to trace
  RETURN: none
  ==========================================================================*/
void uma_fsm_histDump (void * objPtr, uma_fsm_t * pfsm) {
  uint8_t                  historyIdx;
  int                    idx;

  if (pfsm->printHistRec == NULL) {
    printf ("No printHistRec function\n");
    return;
  }

  /* retrieve the latest history record */
  historyIdx = pfsm->curHistIdx;
  UMA_FSM_NEXT_IDX(historyIdx);

  printf("HISTORY\n");

  /* Loop on all the records from the older record */
  for (idx=0; idx < UMA_FSM_MAX_HISTORY; idx++) {
    /* Display only the record */
    (pfsm->printHistRec) (TRUE,pfsm->histRec[historyIdx].word[0], pfsm->histRec[historyIdx].word[1]);
    /* Next record */
    UMA_FSM_NEXT_IDX(historyIdx);
  }

  /* End displaying the current flags and state */
  (pfsm->printHistRec) (TRUE,pfsm->pflags[0], (pfsm->fsm_state & 0xFF)|(pfsm->pflags[1] & 0xFFFFFF00));
}

/*__________________________________________________________________________
  format the whole history of an FSM
  ==========================================================================
  PARAMETERS:
  . buff : address where to write the formated history
  . pfsm : address of the FSM context
  RETURN:
  . the address of the end of the formated string
  ==========================================================================*/
char * uma_fsm_wholeHistFormat (char * buff, uma_fsm_t * pfsm) {
  uint8_t                  historyIdx;
  int                    idx;

  if (pfsm->formatHist == NULL) {
    buff += sprintf (buff,"No history format function\n");
    return buff;
  }

  /* retrieve the latest history record */
  historyIdx = pfsm->curHistIdx;
  UMA_FSM_NEXT_IDX(historyIdx);

  /* Loop on all the records from the older record */
  for (idx=0; idx < UMA_FSM_MAX_HISTORY; idx++) {
    /* Display only the record */
    buff = (pfsm->formatHist) (buff,TRUE,pfsm->histRec[historyIdx].word[0], pfsm->histRec[historyIdx].word[1]);
    /* Next record */
    UMA_FSM_NEXT_IDX(historyIdx);
  }
  return buff;
}
