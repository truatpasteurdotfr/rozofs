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
#include <string.h>
#include <errno.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/list.h>
#include <rozofs/common/log.h>
#include "rozofsmount.h"
#include <rozofs/core/ruc_buffer_api.h>
#include <rozofs/core/uma_dbg_api.h>
#include "rozofs_sharedmem.h"
/**
* array used for storing information related to the storcli shared memory
*/
rozofs_shared_pool_t rozofs_storcli_shared_mem[STORCLI_PER_FSMOUNT];

static char localBuf[8192];

/*__________________________________________________________________________
*/
/**
* display the configuration of the shared memories associated with the storcli
*/
void rozofs_shared_mem_display(char * argv[], uint32_t tcpRef, void *bufRef)
{
    char *pChar = localBuf;
    int i;


    pChar += sprintf(pChar, "   storcli  |     key     |  size    | count |    address     | read stats     |\n");
    pChar += sprintf(pChar, "------------+-------------+----------+-------+----------------+----------------+\n");
    for (i = 0; i < STORCLI_PER_FSMOUNT; i ++)
    {
      pChar +=sprintf(pChar,"        %d   | %8.8d  | %8.8d |%4d   | %p |%15llu |\n",i,rozofs_storcli_shared_mem[i].key,
                      rozofs_storcli_shared_mem[i].buf_sz,
                      rozofs_storcli_shared_mem[i].buf_count,
                      rozofs_storcli_shared_mem[i].data_p,
                      (long long unsigned int)rozofs_storcli_shared_mem[i].read_stats);    
    
    }
    uma_dbg_send(tcpRef, bufRef, TRUE, localBuf);

}
/*
 *________________________________________________________
 */
 /**
 *  API to create the shared memory used for reading data from storcli's
 
   note: the instance of the shared memory is the concatenantion of the rozofsmount 
         instance and storcli instance: (rozofsmount<<1 | storcli_instance) 
         (assuming of max of 2 storclis per rozofsmount)
         
   @param : key_instance : instance of the shared memory (lower byte of the key)
   @param : storcli_idx : instance of the shared memory
   @param buf_nb: number of buffers
   @param buf_sz: size of the buffer payload
   
   @retval 0 on success
   @retval < 0 on error (see errno for details
 */
 
int rozofs_create_shared_memory(int key_instance,int storcli_idx,uint32_t buf_nb, uint32_t buf_sz)
{
   key_t key = 0x524F5A30 | key_instance;

   rozofs_storcli_shared_mem[storcli_idx].key          = key;
   rozofs_storcli_shared_mem[storcli_idx].buf_sz       = buf_sz;
   rozofs_storcli_shared_mem[storcli_idx].buf_count    = buf_nb;
   rozofs_storcli_shared_mem[storcli_idx].pool_p = ruc_buf_poolCreate_shared(buf_nb,buf_sz,key);
   if (rozofs_storcli_shared_mem[storcli_idx].pool_p == NULL) return -1;
   rozofs_storcli_shared_mem[storcli_idx].data_p = ruc_buf_get_pool_base_data(rozofs_storcli_shared_mem[storcli_idx].pool_p);
   return 0;
  
}

/*
 *________________________________________________________
 */
/**
*  Init of the shared memory structure seen by Rozofsmount

  @param none
  
  @retval none
*/
void rozofs_init_shared_memory()
{
  int i;
  
  for (i = 0; i < STORCLI_PER_FSMOUNT; i++)
  {
    memset(&rozofs_storcli_shared_mem[i],0,sizeof(rozofs_shared_pool_t));
  
  }
}
