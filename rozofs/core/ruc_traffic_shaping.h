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
 
#ifndef RUC_TRAFFIC_SHAPING_H
#define RUC_TRAFFIC_SHAPING_H

 #include <stdio.h>
#include <stdlib.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <fcntl.h> 
#include <sys/un.h>             
#include <sys/time.h>
#include <errno.h>  
#include <rozofs/common/types.h>
#include <rozofs/common/log.h>
#include "ruc_common.h"
#include "ruc_list.h"
#include "ruc_buffer_api.h"
#include "uma_dbg_api.h"

#define TRSHAPE_MAX_CTX 10
 
typedef struct _trshape_stats_t
{
   uint64_t submit_count;   /**< number of packet submitted to the shaper */
   uint64_t enqueued_count;   /**< number of packet that has been enqueued */
   uint64_t dequeued_count;   /**< number of packet that has been dequeued */
   uint64_t forcedeq_count;   /**< number of packet that has been dequeued */
   uint64_t send_violation_count;   /**< number of noempty to empty */
   uint64_t anticipation_count;   /**< number of time a frame is sent on next_time */
   uint64_t deadline_count;       /**< number of time a frame is sent on 2*next_time */
} trshape_stats_t;

#define RUC_BYTE_QUANTUM_PERCENT_DEFAULT 10   /**< percentage of the optimal byte time */


 typedef struct _trshape_ctx_t
{
  ruc_obj_desc_t      link;            /**< To be able to chain the MS context on any list */
  uint32_t            index;           /**< Index of the MS */
  uint32_t            free;            /**< Is the context free or allocated TRUE/FALSE */
  uint32_t            integrity;       /**< the value of this field is incremented at  each MS ctx allocation */
  uint64_t            next_time;       /**< scheduler timestamp for traffic shaping  */
  uint32_t            quantum_percent;         /**< percentage of byte time used for adjustement*/
  uint32_t            anticipation;         /**< counter incremented on xmit when "sent" is already 1*/
  uint64_t            byte_time_ns;    /**< byte time in 1/10 of nanosecond */
  uint64_t            byte_time_ns_adjusted;    /**< byte time in 1/10 of nanosecond : used to evaluate the transmission time*/
  uint32_t            sent;    /**< assert to 1 when frame is sent on next_time */
  uint32_t            noempty2empty;    /**< assert to 1 when the queue becomes empty, cleared on resp. received */
  trshape_stats_t     stats;
} trshape_ctx_t;


//#define RUC_TRAFFIC_SHAPING_ADJUST_BYTE_TIME_ADD(p) p->byte_time_ns_adjusted += (p->byte_time_ns_adjusted *p->quantum_percent)/100;

static inline void RUC_TRAFFIC_SHAPING_ADJUST_BYTE_TIME_ADD(trshape_ctx_t *p) {
        int64_t adjusted_time;
       
       adjusted_time =   p->byte_time_ns_adjusted + (p->byte_time_ns_adjusted *p->quantum_percent)/100;
      if (adjusted_time > (2 *p->byte_time_ns))  p->byte_time_ns_adjusted = 2 * p->byte_time_ns;
      else p->byte_time_ns_adjusted = adjusted_time;
 
}


static inline void RUC_TRAFFIC_SHAPING_ADJUST_BYTE_TIME_SUB(trshape_ctx_t *p) 
{ 
   int64_t adjusted_time; 
   p->byte_time_ns_adjusted = p->byte_time_ns;
   return;
   
       adjusted_time = p->byte_time_ns_adjusted; 
       adjusted_time -=(p->byte_time_ns *p->quantum_percent)/100;
      if (adjusted_time < p->byte_time_ns)  p->byte_time_ns_adjusted = p->byte_time_ns;
      else p->byte_time_ns_adjusted = adjusted_time;
}

/**
* Global data
*/

extern trshape_ctx_t storcli_traffic_shaper[]; 

/**
*  fake function
*/
trshape_ctx_t *trshape_get_ctx_from_idx(int idx);

/*__________________________________________________________________________
*/
/**
* schedule a traffic shaping queue

  @param p : traffic shaping queue 
  @param cur_time: current time in us
  
  retval 0 : OK
  retval < 0 error (no scheduler queue)
*/
int trshape_schedule_queue(trshape_ctx_t *p,uint64_t cur_time);


/*__________________________________________________________________________
*/
/**
* queue a buffer in the traffic shaping queue

  @param traffic_shaper_idx 
  @param *buf_p
  @param disk_time
  @param rsp_size
  
  retval 0 : send is granted
  retval 1 : packet is queued
  retval < 0 error (no scheduler queue)
*/
int trshape_queue_buf(int traffic_shaper_idx,void *buf_p,
                      uint32_t rsp_size,uint32_t disk_time,sched_pf_buf_t cbk,int lbg_id);

/*__________________________________________________________________________
*/
/**
* reschedule on frame receive

  @param p : traffic shaping queue 
  @param cur_time: current time in us
  
  retval 0 : OK
  retval < 0 error (no scheduler queue)
*/
int trshape_schedule_on_response();
/*__________________________________________________________________________
*/
/**
*   Init of the traffic shapping function
*

   @param byte_time_ns : default byte time in 1/10 of ns
   
   @retval 0
*/
void trshape_module_init(uint64_t byte_time_ns);


#endif
