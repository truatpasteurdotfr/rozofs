
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "rozofs_timer_conf.h"


rozofs_configure_param_t rozofs_timer_conf[TMR_MAX_ENTRY];


#define DEF_TMR(name,min,max,default,ms_or_sec) { \
 rozofs_timer_conf[name].default_val = default; \
 rozofs_timer_conf[name].min_val = min; \
 rozofs_timer_conf[name].max_val = max; \
 rozofs_timer_conf[name].cur_val = default;\
 rozofs_timer_conf[name].unit = ms_or_sec;\
}
/*__________________________________________________________________________
*/
/**
*  Initialization of the parameter associated with the different
*  Guard timers used by rozofs
 @param none
 @retval none
*/
void rozofs_tmr_init_configuration()
{
  DEF_TMR(TMR_EXPORT_PROGRAM,4,30,25,TMR_SEC); /**< exportd transaction timeout :default 25 s */
  DEF_TMR(TMR_STORAGE_PROGRAM,2,30,3,TMR_SEC);  /**< storaged transaction timeout : default 3 s  */
  DEF_TMR(TMR_STORCLI_PROGRAM,2,30,10,TMR_SEC);            /**< storagd client transaction timeout :      default 10 s */
  DEF_TMR(TMR_EXPORTD_PROFILE_PROGRAM,5,30,25,TMR_SEC);     /**< exportd profiler program                  default 25 s */
  DEF_TMR(TMR_ROZOFSMOUNT_PROFILE_PROGRAM,5,30,25,TMR_SEC); /**< rozofsmount profiler program              default 25 s */
  DEF_TMR(TMR_MONITOR_PROGRAM,2,30,4,TMR_SEC);             /**< storaged monitor program                  default 4 s  */
  DEF_TMR(TMR_STORAGED_PROFILE_PROGRAM,2,30,25,TMR_SEC);    /**< storaged profiler program                 default 25 s */
  DEF_TMR(TMR_STORCLI_PROFILE_PROGRAM,2,30,25,TMR_SEC);     /**< storaged client profiler program          default 25 s */
  /*
  ** timer related to dirent cache
  */
  DEF_TMR(TMR_FUSE_ATTR_CACHE,2,300,10,TMR_SEC);            /**< attribute cache timeout for fuse           default 10 s */
  DEF_TMR(TMR_FUSE_ENTRY_CACHE,2,300,10,TMR_SEC);           /**< entry cache timeout for fuse               default 10 s */
  /*
  ** dirent cache timer
  */
  
  /*
  ** timer related to TCP connection and load balancing group
  */
  DEF_TMR(TMR_TCP_FIRST_RECONNECT,2,10,2,TMR_SEC);        /**< TCP timer for the first TCP re-connect attempt  default   2 s */
  DEF_TMR(TMR_TCP_RECONNECT,2,30,4,TMR_SEC);              /**< TCP timer for subsequent TCP re-connect attempts  default 4 s */
  DEF_TMR(TMR_RPC_NULL_PROC_TCP,2,30,3,TMR_SEC);          /**< timer associated to a null rpc procedure polling initiated from TCP cnx default 3 s */
  DEF_TMR(TMR_RPC_NULL_PROC_LBG,3,30,4,TMR_SEC);          /**< timer associated to a null rpc procedure polling initiated from TCP cnx default 4 s */
  /*
  ** timer related to projection read/write
  */
  DEF_TMR(TMR_PRJ_READ_SPARE,20,5000,50,TMR_MS);            /**< guard timer started upon receiving the first projection (read) default 100 ms */

}
/*__________________________________________________________________________
*/
/**
*  Configure one timer of rozofs

 @param timer_id: index of the timer (name: see rozofs_timer_e)
 @param val : value of the timer (see the definition of the timer for the unit)
 
 @retval 0 on success
 @retval -1 on error (see errno for details)
*/
int rozofs_tmr_configure(int timer_id,int val)
{
rozofs_configure_param_t *p; 

  if (timer_id >= TMR_MAX_ENTRY)
  {
    errno = EINVAL; 
    return -1; 
  }
  p = &rozofs_timer_conf[timer_id];
  if ((val < p->min_val) ||  (val > p->max_val))
  {
    errno = ERANGE;
    return -1;   
  }
  p->cur_val = val;
  return 0;
}

/*__________________________________________________________________________
*/
/**
*  Configure one timer of rozofs

 @param  timer_id: index of the timer (name: see rozofs_timer_e)
 
 @retval 0 on success
 @retval -1 on error (see errno for details)
*/
int rozofs_tmr_set_to_default(int timer_id)
{
  rozofs_configure_param_t *p; 

  if (timer_id >= TMR_MAX_ENTRY)
  {
    errno = EINVAL; 
    return -1; 
  }
  p = &rozofs_timer_conf[timer_id];
  p->cur_val = p->default_val;
  return 0;
}

#define DISPLAY_TMR(name) \
{ \
  buf+=sprintf(buf," %-27s | %3d | %7d | %5d | %5d | %7d | %4s |\n",\
       #name,i,p->default_val,p->min_val,p->max_val,p->cur_val,(p->unit==TMR_SEC)?"sec":"ms");\
  p++;i++;\
}

char *rozofs_tmr_display(char *buf)
{
  rozofs_configure_param_t *p=&rozofs_timer_conf[0];
  int i= 0;
  
buf+=sprintf(buf,"    timer name               | idx | default |  min  | max   | current | unit |\n");
buf+=sprintf(buf,"-----------------------------+-----+---------+-------+-------+---------+------+\n");

  DISPLAY_TMR(export_program); /**< exportd transaction timeout :default 25 s */
  DISPLAY_TMR(storage_program);  /**< storaged transaction timeout : default 3 s  */
  DISPLAY_TMR(storcli_program);            /**< storagd client transaction timeout :      default 10 s */
  DISPLAY_TMR(exportd_profile_program);     /**< exportd profiler program                  default 25 s */
  DISPLAY_TMR(rozofsmount_profile_program); /**< rozofsmount profiler program              default 25 s */
  DISPLAY_TMR(monitor_program);             /**< storaged monitor program                  default 4 s  */
  DISPLAY_TMR(storaged_profile_program);    /**< storaged profiler program                 default 25 s */
  DISPLAY_TMR(storcli_profile_program);     /**< storaged client profiler program          default 25 s */
  /*
  ** timer related to dirent cache
  */
  DISPLAY_TMR(fuse_attr_cache);            /**< attribute cache timeout for fuse           default 10 s */
  DISPLAY_TMR(fuse_entry_cache);           /**< entry cache timeout for fuse               default 10 s */
  /*
  ** dirent cache timer
  */
  
  /*
  ** timer related to tcp connection and load balancing group
  */
  DISPLAY_TMR(tcp_first_reconnect);        /**< tcp timer for the first tcp re-connect attempt  default   2 s */
  DISPLAY_TMR(tcp_reconnect);              /**< tcp timer for subsequent tcp re-connect attempts  default 4 s */
  DISPLAY_TMR(rpc_null_proc_tcp);          /**< timer associated to a null rpc procedure polling initiated from tcp cnx default 3 s */
  DISPLAY_TMR(rpc_null_proc_lbg);          /**< timer associated to a null rpc procedure polling initiated from tcp cnx default 4 s */
  /*
  ** timer related to projection read/write
  */
  DISPLAY_TMR(prj_read_spare);            /**< guard timer started upon receiving the first projection (read) default 100 ms */
  return buf;

}



