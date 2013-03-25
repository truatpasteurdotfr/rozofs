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

#define UMA_FSM_FRAME_WORK_C

#include <stdio.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_list.h"
#include "uma_fsm_framework.h"

uint8_t uma_isModuleRunning(uint8_t moduleId);
void uma_fsm_insert_ReadyQ(uma_fsm_t *pfsm);
/*
**-----------------------------------------------------------
**
**  Default handler attached to a FSM
**
**
**    IN:
**         pObjRef : pointer to the object that owns the fsm
**         p       : pointer to the fsm context
**
**    OUT:
**         none
**---------------------------------------------------------------
*/


void uma_no_execute_fsm( void *pObjRef,uma_fsm_t *p)
{
  printf ("no entry FSM point for FSM %p of Object %p\n",p,pObjRef);
  return;
}




/*
**-----------------------------------------------------------
**
**  The purpose of that function is to set up the common attributes
**  of the FSM engine.
**
**  Note: if a trace prg is not used, the input parameter should
**        be NULL
**
**    IN:
**         pObjRef : pointer to the object that owns the fsm
**         p       : pointer to the fsm context
**        moduleId : module ID under which the FSM is runnable
**        execute_fsm : FSM entry point code
**        pflags : pointer to the event array (2 uint32_t), only 32+24 are
**                 logged in the history.    .
**
**    OUT:
**         none
**---------------------------------------------------------------
*/

void uma_fsm_engine_init(void 	       *pObjRef,
                         uma_fsm_t     *pfsm,
			 uint8_t         moduleId,
		         exec_fsm_t    execute_fsm,
			 print_fsm_t   printHistRec,
			 uint32_t        *pflags)
{


  pfsm->fsm_active = FALSE;
  pfsm->fsm_activeAgain = FALSE;
  pfsm->moduleId = moduleId;
  pfsm->objPtr = pObjRef;
  pfsm->trace = FALSE;

  /*
  ** record the pointer to the event array used by the state machine
  */
  pfsm->pflags = pflags;
  pflags[0] = 0;
  pflags[1] = 0;

  if (execute_fsm !=NULL)
    pfsm->execute_fsm = execute_fsm;
  else
    pfsm->execute_fsm = (exec_fsm_t)uma_no_execute_fsm;

  pfsm->printHistRec = printHistRec;
  pfsm->formatHist = NULL;

  /*
  **  clear the history buffer
  */
  uma_fsm_history_init(pfsm);
   /*
   ** put the link area in a free state
   */
   ruc_listEltInit((ruc_obj_desc_t*)pfsm);
}

void uma_fsm_engine_formatHist(uma_fsm_t     *pfsm,
			       formatHist_t  format)
{

  pfsm->formatHist = format;
}
/*
**-----------------------------------------------------------
**
**  That procedure is called each time an event that can
**  be used in one transition of the state machine is changed.
**
**  If the state machine is inactive and the current module
**  in which it execute is running, then the entry point
**  of the state machine code is called.
**
**  If the module is not runnable, then the state machine is
**  inserted in the READY list of the module and the an internal
**  event is posted to the module
**
**
**    IN:
**         pObjRef : pointer to the object that owns the fsm
**         p       : pointer to the fsm context
**
**    OUT:
**         none
**---------------------------------------------------------------
*/
void uma_fsm_engine (void *pObjRef,
                     uma_fsm_t *pfsm
		      )
{
   if (uma_isModuleRunning(pfsm->moduleId) == FALSE)
   {
     /*
     **  The module is not currently the active one
     **  the state machine has to be queued in the READY
     **  state queue of the module.
     **  If the ready queue was empty, an internal event
     **  is posted on the event socket associated to the
     **  module
     */
     uma_fsm_insert_ReadyQ(pfsm);
     return;

   }

   if (pfsm->fsm_active == TRUE)
   {
     /*
     ** the fsm is already active. Just indicates
     ** that it should be re-activated
     */
     pfsm->fsm_activeAgain = TRUE;
     return;
   }
   /*
   **  the fsm is not active: starts it
   */
   pfsm->fsm_active = TRUE;
   while (pfsm->fsm_active == TRUE)
   {
     pfsm->fsm_activeAgain = FALSE;
     (pfsm->execute_fsm)(pObjRef,pfsm);
     if (pfsm->fsm_activeAgain == FALSE)
     {
       /*
       ** that the end
       */
       pfsm->fsm_active = FALSE;
       break;
     }
   }
}

