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
#ifndef UMA_FSM_XXX_H
#define UMA_FSM_XXX_H

#include <stdio.h>

#include <rozofs/common/types.h>

#include "ruc_common.h"
#include "ruc_list.h"

typedef void (*exec_fsm_t)(void *objPtr,void *pfsm);
typedef void (*print_fsm_t)(uint8_t printState, uint32_t flag0, uint32_t flag1);
typedef char * (*formatHist_t) (char * buff, uint8_t printState, uint32_t flag0, uint32_t flag1);
typedef uint32_t* (*trc_fsm_t)(void *objPtr,void *pfsm);

#define UMA_FSM_MAX_HISTORY   8
/*
**  structure of the history record entry
*/
typedef struct _fsm_hist_record_s
{
   uint32_t word[2];    /* word[0] ->events: 0-31 */
                      /* word[1]-> 31-8 : events (32-63), 7-0: state */
} fsm_hist_record_s;

#define FSM_SET(pfsm,state) { pfsm->fsm_state = state; pfsm->fsm_action = TRUE; uma_fsm_trace(TRUE,pfsm->objPtr,pfsm);break;}

#define FSM_ACTION_BEGIN        if (pfsm->fsm_action) {
#define FSM_ACTION_END          pfsm->fsm_action = FALSE;}

#define FSM_TRANS_BEGIN(x)      if(x) {uma_fsm_trace(FALSE,pfsm->objPtr,pfsm);
#define FSM_TRANS_END           break; }

#define FSM_STATE_BEGIN(x)      case x:
#define FSM_STATE_END           return;
/*
**  FSM common structure
*/
typedef struct uma_fsm_t
{
   ruc_obj_desc_t link;  /* used to queue the FSM within the READY FSM list */
   uint8_t   fsm_state;
   uint8_t   fsm_active:1;  /* TRUE/FALSE */
   uint8_t   fsm_activeAgain:1;  /* TRUE/FALSE */
   uint8_t   fsm_action:1;   /* when TRUE the action of the state is executed */
   uint8_t   trace;
   uint8_t   bit5_8:4;

   uint8_t   moduleId;         /* reference of the module under which the state machine
                             ** is running
			     */
   uint8_t   curHistIdx;       /* current index with the history record */
   fsm_hist_record_s histRec[UMA_FSM_MAX_HISTORY];  /* history buffer */
   void	   *objPtr;          /* pointer to the object that owns the FSM  */
   exec_fsm_t execute_fsm;  /* entry point of the FSM code */
   print_fsm_t printHistRec; /* Entry point of the FSM record display */
   formatHist_t formatHist;/* Entry point of the FSM record display */
   uint32_t  *pflags;         /* address of the event flags area (8 bytes only) */
} uma_fsm_t;



/*
**    P U B L I C    A P I
*/
/*__________________________________________________________________________
  format the whole history of an FSM
  ==========================================================================
  PARAMETERS:
  . buff : address where to write the formated history
  . pfsm : address of the FSM context
  RETURN:
  . the address of the end of the formated string
  ==========================================================================*/
void uma_fsm_engine_formatHist(uma_fsm_t     *pfsm,
			       formatHist_t  format);
/*__________________________________________________________________________
  temporary to declare a history formating function
  ==========================================================================*/
char * uma_fsm_wholeHistFormat (char * buff, uma_fsm_t * pfsm);
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
                         uma_fsm_t     *p,
			 uint8_t         moduleId,
		         exec_fsm_t    execute_fsm,
			 print_fsm_t   printHistRec,
			 uint32_t        *pflags);




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
                     uma_fsm_t *p
		      );


/*
**    P R I V A T E     A P I
*/
void uma_fsm_history_init(uma_fsm_t * pfsm);
void uma_fsm_display_record (fsm_hist_record_s * record);
void uma_fsm_trace (uint8_t new, void * objPtr, uma_fsm_t * pfsm);
void uma_fsm_histDump (void * objPtr, uma_fsm_t * pfsm);
void uma_fsm_set_trace (uma_fsm_t * pfsm,uint8_t status);

#endif

