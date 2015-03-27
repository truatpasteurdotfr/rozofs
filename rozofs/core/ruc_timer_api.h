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
#ifndef VSTK_TIMERH
#define VSTK_TIMERH


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <rozofs/common/types.h>

/*------------------------------
 *  Synonyms for basic C types
 *------------------------------
 */

typedef char              SBYTE;        /* synonym for signed character */
typedef unsigned char     UBYTE;        /* synonym for unsigned character */
typedef short             SWORD;        /* synonym for signed short int */
typedef unsigned short    UWORD;        /* synonym for unsigned short int */
typedef long             SLWORD;        /* synonym for signed long int */
typedef unsigned long    ULWORD;        /* synonym for unsigned long int */
#ifdef HSS_POWER_PC
/* synonym for unsigned long long  = 8 bytes  */
typedef unsigned long long int    ULLWORD;
/* synonym for signed long long   */
typedef long long int    SLLWORD;
#endif
/*
 *------------------------------------
 *  basic macros to be used when
 *  declaring or defining a function
 *------------------------------------
 */

#define IN                              /* input parameter */
#define OUT                             /* output parameter */
#define INOUT                           /* input and output parameter */
/*

typedef unsigned   char    uint8_t;
*/

typedef unsigned   long    timer_val_t;    /* Value of a timer */


#ifndef NULL
#define NULL 0
#endif


#ifndef P_NIL
#define P_NIL     ((void *)(0))       /* null pointer */
#endif
#define TIMER_ID_NIL	(uint8_t FAR *)0	/* timer ID NULL */
#define X_NIL           0x7fff /* time slot index not significant */

#       define TIMER_ON                               (1)
#       define TIMER_OFF                              (2)

#ifndef OK
#       define OK                               (3)
#endif
#ifndef NOK
#       define NOK                              (4)
#endif

#ifndef FALCON_NVAL
#       define   FALCON_NVAL                       (int) (-1)
#endif




				/*-=-=-=-=-=-=-=-=-=-=-=-*/
				/*                       */
				/*   TIMERS MANAGEMENT   */
				/*                       */
				/*-=-=-=-=-=-=-=-=-=-=-=-*/



typedef enum {
    TIMER_SLOT_SIZE_32  = 32,      /* 32 entries           */
    TIMER_SLOT_SIZE_64  = 64,      /* 64 entries           */
    TIMER_SLOT_SIZE_128 = 128,     /* 128 entries           */
    TIMER_SLOT_SIZE_256 = 256      /* 256 entries          */
} TIMER_SLOT_SIZE_E ;

typedef enum {
    TIMER_TICK_VALUE_10MS  = 10,      /* tick value = 50 ms   */
    TIMER_TICK_VALUE_20MS  = 20,      /* tick value = 50 ms   */
    TIMER_TICK_VALUE_50MS  = 50,      /* tick value = 50 ms   */
    TIMER_TICK_VALUE_100MS  = 100,    /* tick value = 100 ms  */
    TIMER_TICK_VALUE_250MS  = 250,    /* tick value = 250 ms  */
    TIMER_TICK_VALUE_500MS = 500,     /* tick value = 500 ms  */
    TIMER_TICK_VALUE_1S = 1000        /* tick value = 1s      */
} TIMER_TICK_VALUE_E ;


/*
  if application utiliezes socket in block mode
     an event is mandatory
  in non blocking mode, a reader/writer system
  with an API provided by timer library could be implemented
  it avoids to use message
  the 2nd part is not implemented but could be done
  easily

 */
typedef enum {
    SOCKET_MODE_BLOCK  = 1,      /* socket blocking mode   */
    SOCKET_MODE_NON_BLOCK  = 2,    /*socket non blocking mode  */
    SOCKET_MODE_NON_BLOCK_API  = 3    /*socket non blocking mode + API */
} TIMER_MODE_SOCKET_E ;



typedef enum {
    TIMER_TICK_REG_RQ  = 1,    /* registration request   */
    TIMER_TICK_REG_RS  = 2,    /* registration response  */
    TIMER_TICK_EVT      = 3    /* tick event  */
} TIMER_MSG_CODE_E ;


struct timer_registration_rq {
  TIMER_MSG_CODE_E	msg_code;   /* message code : TIMER_TICK_REG_RQ */

  /* application characteristics */

  TIMER_MODE_SOCKET_E 	socket_mode;	 /* socket mode : block
					    non blocking
					    non blocking + API */
  int 	tick_value;	/* wake up period in ms for time
			   management embedded in  application
			   min value = 50 ms */

  uint8_t 	app_id;		/* application identifier */
  uint8_t 	snap_id;       	/* sub net app id */
  void (*p_fct) (void );          /* API entry point in application
				   (if socket_mode = non blocking + API */
 };

struct timer_registration_rs{
  TIMER_MSG_CODE_E	msg_code;      	/* message code : TIMER_TICK_REG_RS */
  int 	retcode;   	        /* OK or  NOK*/
  };

struct timer_event{
  TIMER_MSG_CODE_E	msg_code;      	/* message code : TIMER_TICK_REG_RS */
  };

				/*----------------------*/
				/* Timer cell structure */
				/*----------------------*/

struct timer_cell {
     struct timer_cell  *p_next;    	/* pointer to next cell */
     struct timer_cell  *p_prior;	/* pointer to prior cell */
     unsigned int 	 to_mod_nb;    /* modulo nb when the time-out will fail */
     unsigned int 	 x_timer_head;	/* index in the timer_head table */
     int		period_flag;    /* ON if the timer is periodic */
  void (*p_fct_api) (void);       /* pointer to the application API */
// 64BITS     void (*p_fct) (int param);       /* pointer to the user function called
     void (*p_fct) (void * param);       /* pointer to the user function called
					   periodically at timer expiration */
// 64BITS     int        fct_param;    /* parameter passed to the called function */
     void *        fct_param;    /* parameter passed to the called function */
    uint8_t      app_id;       /* network access identifier */
     uint8_t      snap_id;      /* service access point identifier */
    timer_val_t to_val;	      /* saved time-out value */
};

#define TIMER_CELL_LGTH		sizeof (struct timer_cell)

			/* Access to timer cell value using p_cell pointer */

#define Cell_next	    ((struct timer_cell  *)p_cell) -> p_next
#define Cell_prior	    ((struct timer_cell  *)p_cell) -> p_prior
#define Cell_to_mod_nb      ((struct timer_cell  *)p_cell) -> to_mod_nb
#define Cell_x_head	    ((struct timer_cell  *)p_cell) -> x_timer_head
#define Cell_period_flag    ((struct timer_cell  *)p_cell) -> period_flag
#define Cell_app_id	    ((struct timer_cell  *)p_cell) -> app_id
#define Cell_snap_id	    ((struct timer_cell  *)p_cell) -> snap_id
#define Cell_p_fct	    ((struct timer_cell  *)p_cell) -> p_fct
#define Cell_p_fct_api	    ((struct timer_cell  *)p_cell) -> p_fct_api
#define Cell_fct_param	    ((struct timer_cell  *)p_cell) -> fct_param
#define Cell_to_val	    ((struct timer_cell  *)p_cell) -> to_val

				/*----------------------*/
				/* Timer slot structure */
				/*----------------------*/

struct timer_head {
	 struct timer_cell *p_first;      /* pointer to the first timer cell */
};

			/* Access to the thread header values using an index */

#define Head_first(x)		p_timer_slot[x].p_first
#define Head_a_cell(x)		(struct timer_cell  *)p_timer_slot[x]


/*--------------------------------------------------------------------------*/


				/*-=-=-=-=-=-=-=-=-=-=-=-*/
				/*                       */
				/*    MESSAGE QUEUEING   */
				/*                       */
				/*-=-=-=-=-=-=-=-=-=-=-=-*/

			/*
				Queue manipulation macro's (independant on the queue
				management location).
				Uses:
					- p_next:  pointer to the next  block in the queue
					- p_prior: pointer to the prior block in the queue
	*/

#define Next(p_xx)		((p_xx) -> p_next)		/* get next  element from element pointed to by p_xx */
#define Prior(p_xx)		((p_xx) -> p_prior)		/* get prior element from element pointed to by p_xx */






			/* Timer management functions */

extern int ruc_timer_init_appli (TIMER_TICK_VALUE_E timer_application_tick,
		       TIMER_SLOT_SIZE_E timer_slot_size1,
			char app_id,
			char snap_id,
		        TIMER_MODE_SOCKET_E socket_mode) ;

extern  int ruc_timer_send_registration(int socket_id,
			TIMER_TICK_VALUE_E timer_application_tick,
			char app_id,
			char snap_id,
		        TIMER_MODE_SOCKET_E socket_mode) ;


extern struct timer_cell  *ruc_timer_alloc (char app_id, char snap_id);

extern void ruc_timer_free ( struct timer_cell   *p_cell) ;

extern void ruc_timer_start (struct timer_cell  *p_cell,
				unsigned long to_val,
// 64BITS				void (*p_fct) (int fct_param) ,
				void (*p_fct) (void * fct_param) ,
// 64BITS				int fct_param);
				void * fct_param);
extern void ruc_timer_stop ( struct timer_cell  *p_cell);

extern void ruc_periodic_timer_start (struct timer_cell *p_cell,
					 unsigned long to_val,
// 64BITS					 void (*p_fct) (int fct_param) ,
					 void (*p_fct) (void * fct_param) ,
// 64BITS					 int fct_param) ;
					 void * fct_param) ;


extern void ruc_timer_process () ;


/* extern void timer_dispatch_rec (char *pPayload ,
			int  PayloadLen,
			struct falcon_sockaddr * remote_addr); */

extern void ruc_timer_tick_write ();

extern   void ruc_timer_tick_rcv_check ();

void ruc_timer_init (TIMER_TICK_VALUE_E timer_application_tick,
			 TIMER_SLOT_SIZE_E timer_slot_size1);


uint32_t ruc_timer_moduleInit(uint32_t active);
  
/**
*  Get the current ruc ticker (in 10 ms unit)
*/
extern uint64_t  ruc_timer_ticker;

static inline uint64_t timer_get_ticker()
{
  return ruc_timer_ticker;
}

#ifdef __cplusplus
}
#endif /*__cplusplus */

#endif
