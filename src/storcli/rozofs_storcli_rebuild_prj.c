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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <rozofs/rozofs.h>
#include <rozofs/rozofs_srv.h>
#include "rozofs_storcli_transform.h"
/*
**__________________________________________________________________________
*/
/**
* Internal function that identifies the Projections that Must be rebuilt

  @param prj_ctx_p: pointer to the projection table context
  @param timestamp_tb_p: pointer to the timestamp table that contains the projection with the same timestamp
  @param found_entry_idx : index of the entry that we be used for rebuilding the data (must be excluded)
  @param nb_entries : number of entries in the timestamp table
*/

void rozofs_storcli_mark_projection2rebuild(rozofs_storcli_projection_ctx_t *prj_ctx_p,
                                            rozofs_storcli_timestamp_ctx_t *timestamp_tb_p,
                                            uint8_t found_entry_idx,
                                            uint8_t nb_entries)
{
   int i;
   rozofs_storcli_timestamp_ctx_t *p;
   uint8_t projection_idx;
   uint8_t projection_id;
   
   for (i = 0 ; i < nb_entries; i++)
   {
      if (i == (int)found_entry_idx) continue;
      p = &timestamp_tb_p[i];
      if (p->count == 0) continue;
      for (projection_idx = 0; projection_idx < p->count; projection_idx++)
      {
         /*
         ** check if the reference of the projection is the same as the reference of the storage
         ** because in that case we can attempt to rebuild it
         */
         projection_id = p->prj_idx_tb[projection_idx];
         if (prj_ctx_p[projection_id].stor_idx ==  projection_id) prj_ctx_p[projection_id].rebuild_req = 1;
      }
   }
}



/*
**__________________________________________________________________________
*/
/**
* Function that check if there is some projection that needs to be rebuilt

  @param prj_ctx_p: pointer to the projection table context
  @param rozof_safe : max number of entries
  
  @retval 0 : no projection to rebuild
  @retval <> 0 : number of projection to rebuild
*/

int rozofs_storcli_check_projection2rebuild(rozofs_storcli_projection_ctx_t *prj_ctx_p,uint8_t rozof_safe)
{
  uint8_t projection_id;
  uint8_t projection_count = 0;
  
  for (projection_id = 0; projection_id < rozof_safe; projection_id++)
  {
     /*
     ** check if the reference of the projection is the same as the reference of the storage
     ** because in that case we can attempt to rebuild it
     */
     if (prj_ctx_p[projection_id].rebuild_req)
     {
       projection_count++;
       printf("-------> Projection to rebuild : %d\n",projection_id);    
     }
  }
  return projection_count;
}
