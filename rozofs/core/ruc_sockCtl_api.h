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
#ifndef SOCKET_CTRL_API_H
#define SOCKET_CTRL_API_H



// 64BITS typedef uint32_t (*ruc_pf_sock_t)(uint32,int);
typedef uint32_t (*ruc_pf_sock_t)(void * objref, int socketid);

typedef struct _ruc_sockCallBack_t
{
   ruc_pf_sock_t  isRcvReadyFunc;
   ruc_pf_sock_t  msgInFunc;
   ruc_pf_sock_t  isXmitReadyFunc;
   ruc_pf_sock_t  xmitEvtFunc;
} ruc_sockCallBack_t;



/*
**   PUBLIC SERVICES API
*/

// 64BITS uint32_t ruc_sockctl_connect(int socketId,
void * ruc_sockctl_connect(int socketId,
                           char *name,
                           uint32_t priority,
			   // 64BITS                            uint32_t objRef,
                           void *objRef,
                           ruc_sockCallBack_t *callback);

// 64BITS uint32_t ruc_sockctl_disconnect(uint32 connectionId);
uint32_t ruc_sockctl_disconnect(void * connectionId);


uint32_t ruc_sockctl_init(uint32_t nbConnection);

void ruc_sockCtrl_selectWait();

/**
*  Init of the system ticker
*/
void rozofs_init_ticker();


/**
* read the system ticker
*/
extern uint64_t rozofs_ticker_microseconds;
static inline uint64_t rozofs_get_ticker_us()
{
  return rozofs_ticker_microseconds;
}

/**
* attach a traffic shaper scheduler

  @param callback
*/
typedef void (*ruc_scheduler_t)(uint64_t cur_time);  

extern ruc_scheduler_t  ruc_applicative_traffic_shaper;
extern ruc_scheduler_t  ruc_applicative_poller;

static inline void ruc_sockCtrl_attach_traffic_shaper(ruc_scheduler_t callback)
{
   ruc_applicative_traffic_shaper = callback;
}


static inline void ruc_sockCtrl_attach_applicative_poller(ruc_scheduler_t callback)
{
   ruc_applicative_poller = callback;
}


#endif
