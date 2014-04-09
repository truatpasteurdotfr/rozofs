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
#ifndef SOCKET_CTRL_H
#define SOCKET_CTRL_H

#include <rozofs/common/types.h>
#include <sys/select.h>
#include "ruc_common.h"
#include "ruc_list.h"
#include "ruc_sockCtl_api.h"

/*
**  max number of priority
*/
#define RUC_SOCKCTL_MAXPRIO 4

#define RUC_SOCKCTL_POLLCOUNT 16 /**< default value for former receiver ready callbacks */
#define RUC_SOCKCTL_POLLFREQ (40*1000) /**< default polling frequency in us */


/*
** connection structure
*/

#define RUC_SOCK_MAX_NAME 48


typedef struct _ruc_sockObj_t
{
   ruc_obj_desc_t   link;
   uint32_t connectId;
   char   name[RUC_SOCK_MAX_NAME];
   int socketId;
   uint32_t priority;
  // 64BITS    uint32_t objRef;
   void   *objRef;
   uint32_t rcvCount;  /* number of times the rcvbit is set */
   uint32_t xmitCount; /* number of times the xmitbit is set */
   uint32_t processed;
   uint32_t lastTime;       /* cpu time in us of last processing */
   uint32_t cumulatedTime;  /* cumulated cpu time of las processings */
   uint32_t nbTimes;       /* NB times it has been scheduled */
   uint32_t lastTimeXmit;       /* cpu time in us of last processing */
   uint32_t cumulatedTimeXmit;  /* cumulated cpu time of las processings */
   uint32_t nbTimesXmit;       /* NB times it has been scheduled */
   ruc_sockCallBack_t *callBack;
} ruc_sockObj_t;



/*
** file descriptor for receiving and transmitting events
*/
extern fd_set  rucRdFdSet;   
extern fd_set  rucWrFdSet;   
extern fd_set  rucWrFdSetCongested;

/*
**  private API
*/

void ruc_sockCtl_checkRcvBits();
void ruc_sockCtl_prepareRcvBits();
void ruc_sockCtl_checkXmitBits();
void ruc_sockCtl_prepareXmitBits();

#endif
