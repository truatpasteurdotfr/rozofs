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
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <rozofs/common/log.h>
#include <rozofs/core/uma_dbg_api.h>
#include <rozofs/rozofs_timer_conf.h>

static char localBuf[4096];



/*__________________________________________________________________________
*/
void dbg_show_tmr(char * argv[], uint32_t tcpRef, void *bufRef) 
{

   rozofs_tmr_display(localBuf);
    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);
}

/*__________________________________________________________________________
*/
void dbg_set_tmr(char * argv[], uint32_t tcpRef, void *bufRef) 
{

   int timer_id,val;
   
   if (argv[2] ==NULL)
   {
    uma_dbg_send(tcpRef, bufRef, TRUE, "missing parameter (%s <tmr_idx> <value>)\n",argv[0]);    
    return;     
   }
   /* Check 1rst a string name */
   timer_id = rozofs_tmr_get_idx_from_name(argv[1]);
   /* Check for an index */
   if (timer_id < 0) {
     errno = 0;
     timer_id = (int) strtol(argv[1], (char **) NULL, 10);   
     if (errno != 0) {
      uma_dbg_send(tcpRef, bufRef, TRUE, "bad tmr_idx (%s <tmr_idx> <value>) %s\n",argv[0],strerror(errno));    
      return;     
     }
   }  
   if (timer_id >= TMR_MAX_ENTRY)
   {
    uma_dbg_send(tcpRef, bufRef, TRUE, "invalid tmr_idx (max %d)\n",(TMR_MAX_ENTRY-1));    
    return;           
   }
   errno = 0;
   val = (int) strtol(argv[2], (char **) NULL, 10);   
   if (errno != 0) {
    uma_dbg_send(tcpRef, bufRef, TRUE, "bad value (%s <tmr_idx> <value>)\n",argv[0]);    
    return;     
   }
   if (rozofs_tmr_configure(timer_id,val) < 0)
   {
    uma_dbg_send(tcpRef, bufRef, TRUE, "timer value out of range\n");    
    return;     
   }   
   uma_dbg_send(tcpRef, bufRef, TRUE, "Success\n");
}


/*__________________________________________________________________________
*/
void dbg_set_tmr_default(char * argv[], uint32_t tcpRef, void *bufRef) 
{

   
   int timer_id;
   
   if (argv[1] ==NULL)
   {
    uma_dbg_send(tcpRef, bufRef, TRUE, "Missing timer identifier (%s <tmr_idx|all> )\n",argv[0]);    
    return;     
   }
   if (strcmp("all",argv[1]) ==0)  
   {
    rozofs_tmr_init_configuration();
    uma_dbg_send(tcpRef, bufRef, TRUE, "Success\n");    
    return;     
   }
   errno = 0;
   timer_id = (int) strtol(argv[1], (char **) NULL, 10);   
   if (errno != 0) {
    uma_dbg_send(tcpRef, bufRef, TRUE, "bad tmr_idx (%s <tmr_idx|all> )\n",argv[0]);    
    return;     
   }
   if (timer_id >= TMR_MAX_ENTRY)
   {
    uma_dbg_send(tcpRef, bufRef, TRUE, "invalid tmr_idx (max %d)\n",(TMR_MAX_ENTRY-1));    
    return;           
   }
   rozofs_tmr_set_to_default(timer_id);
   uma_dbg_send(tcpRef, bufRef, TRUE, "Success\n");    
   return;   
}



/*__________________________________________________________________________
*/
void rozofs_timer_conf_dbg_init()
{
  uma_dbg_addTopic("tmr_show", dbg_show_tmr);
  uma_dbg_addTopic("tmr_set", dbg_set_tmr);
  uma_dbg_addTopic("tmr_default", dbg_set_tmr_default);
}
