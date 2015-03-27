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
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>
#include "export.h"
#include "export_share.h"
#include <rozofs/core/uma_dbg_api.h>




exportd_shared_t exportd_shared_mem;


void show_export_slave(char * argv[], uint32_t tcpRef, void *bufRef) 
{
    int i;
    char *pChar = uma_dbg_get_buffer();
    char buffer[256];
    time_t elapse;
    int days, hours, mins, secs;
    export_share_info_t *q;
         
    if ( exportd_shared_mem.active == 0)
    {
    
       sprintf(pChar,"No shared memory!!\n");
       goto out;
    }    
    q= (export_share_info_t *)exportd_shared_mem.data_p;

    // Compute uptime for storaged process



    pChar += sprintf(pChar, "id   |    pid     |     state       |  uptime             | observer port  | metadata port  | reload count |\n");
    pChar += sprintf(pChar, "-----+------------+-----------------+---------------------+----------------+----------------+--------------+\n");
    
    for (i = 0; i <= EXPORT_SLICE_PROCESS_NB; i++,q++)
    {
      pChar += sprintf(pChar," %d   |",i);
      pChar += sprintf(pChar," %8d   |",q->pid);
      pChar += sprintf(pChar," %-13s   |",q->state);
      elapse = (int) (time(0) - q->uptime);
      days = (int) (elapse / 86400);
      hours = (int) ((elapse / 3600) - (days * 24));
      mins = (int) ((elapse / 60) - (days * 1440) - (hours * 60));
      secs = (int) (elapse % 60);
      sprintf(buffer,"%d days, %d:%d:%d",  days, hours, mins, secs);
      pChar += sprintf(pChar,"%-18s   |",buffer);
      
      pChar += sprintf(pChar,"  %8d      |",q->debug_port);
      pChar += sprintf(pChar,"  %8d      |",q->metadata_port);
      pChar += sprintf(pChar," %8d     |",q->reload_counter);
      pChar += sprintf(pChar,"\n");    
    }

out:       
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
}

/**
* attach or create the exportd shared memory
  THat shared memory is used to provide the exportd slave statistics
  
  @param p : pointer to the configuration parameters
  
  @retval 0 on success
  @retval -1 on error
*/
int export_sharemem_create_or_attach(exportd_start_conf_param_t *p)
{
  key_t key = 0x4558504F | 0x1;
  export_share_info_t *q;
  
  int shmid;

  exportd_shared_mem.key =key;
  exportd_shared_mem.slave_count = EXPORT_SLICE_PROCESS_NB;
  exportd_shared_mem.buf_sz = sizeof(export_share_info_t);
  exportd_shared_mem.data_p  = NULL;
  exportd_shared_mem.slave_p = NULL;
  exportd_shared_mem.active  = 0;
  
  if (p->slave==0)
  {
    /*
    ** delete previous segment because the size might have been changed
    */
    if ((shmid = shmget(exportd_shared_mem.key, sizeof(int),0666 )) >= 0) 
    {

      if (shmctl(shmid, IPC_RMID, NULL) < 0)
      {
          severe("cannot delete segment %s",strerror(errno));       

      }
    }
    /*
    ** create the shared memory
    */
    if ((shmid = shmget(exportd_shared_mem.key, exportd_shared_mem.buf_sz*exportd_shared_mem.slave_count, IPC_CREAT | 0666 )) < 0) 
    {
	severe("shmget %s",strerror(errno));
	exportd_shared_mem.error = errno;
	exportd_shared_mem.active = 0;
	return -1;
    }

  }
  else
  {
    if ((shmid = shmget(exportd_shared_mem.key, 
                       exportd_shared_mem.slave_count*exportd_shared_mem.buf_sz, 
                       0666)) < 0) 
    {
      severe("error on shmget %d : %s",exportd_shared_mem.key,strerror(errno));
      exportd_shared_mem.error = errno;
      exportd_shared_mem.active = 0;
      return -1;
    } 
    info("slave port %d instance %d\n",p->debug_port,p->instance); 
  
  }
  /*
  * Now we attach the segment to our data space.
  */
  if ((exportd_shared_mem.data_p = shmat(shmid, NULL, 0)) == (char *) -1)
  {
      severe("error on shmat %d : %s",exportd_shared_mem.key,strerror(errno));
      exportd_shared_mem.error = errno;
      exportd_shared_mem.active = 0;
      return -1;
  }
  if (p->slave==0) memset(exportd_shared_mem.data_p,0,exportd_shared_mem.slave_count*exportd_shared_mem.buf_sz);

  exportd_shared_mem.active = 1;
  q= (export_share_info_t *)exportd_shared_mem.data_p;
  q = q + p->instance;
  exportd_shared_mem.slave_p = q;
  q->debug_port = p->debug_port;
  q->uptime = (uint64_t)time(0);
  q->pid = getpid();
  sprintf(q->state,"starting");
  
  return 0;
}


/**
*  change the state of the exportd slave
  THat shared memory is used to provide the exportd slave statistics
  
  @param state : new state
  
  @retval none
*/
void export_sharemem_change_state(char *state)
{
  export_share_info_t *q;

 if (exportd_shared_mem.active != 1) return;
 q = (export_share_info_t*) exportd_shared_mem.slave_p;
 strcpy(q->state,state);
}


/**
*  increment the reload counter of the exportd slave
  THat shared memory is used to provide the exportd slave statistics
    
  @retval none
*/
void export_sharemem_increment_reload()
{
  export_share_info_t *q;

 if (exportd_shared_mem.active != 1) return;
 q = (export_share_info_t*) exportd_shared_mem.slave_p;
 q->reload_counter++;
}


/**
*  set the listening port used for metadata for the exportd slave
  THat shared memory is used to provide the exportd slave statistics
  
  @param port: metadata port value
  
  @retval none
*/
void export_sharemem_set_listening_metadata_port(uint16_t port)
{
  export_share_info_t *q;

 if (exportd_shared_mem.active != 1) return;
 q = (export_share_info_t*) exportd_shared_mem.slave_p;
 q->metadata_port = port;
}
