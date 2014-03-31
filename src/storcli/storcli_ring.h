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

#ifndef STORCLI_RING_H
#define STORCLI_RING_H

#define STORCLI_RING_SZ 128
#define STORCLI_RUN  1
#define STORCLI_WAIT 0

extern uint64_t stc_rng_collision_count;
extern uint64_t stc_rng_submit_count;
extern uint64_t stc_rng_parallel_count;
extern uint16_t stc_rng_rd_idx;
extern uint16_t stc_rng_wr_idx;

extern int      stc_rng_serialize; 

/*
**____________________________________________________________
*/
/**
*   init of the ring 

   @param none
   
   @retval 0 on success
   @retval < 0 error, see errno for details

*/
int stc_rng_init();

/*
**____________________________________________________________
*/
/**
*     ring insertion:

    insert the new entry in the ring. During the insert the service
    check if the request is runnable.
    
    @param obj_ptr : pointer to the object to insert
    @param opcode : opcode of the request
    @param fid : unique identifier of the object (key)
    @param bid : block index (key)
    @param nb_blks : number of blocks (key)
    @param entry_idx_p : pointer to the entry idx that has been allocated in the ring
    
    @retval 0 : non runnable
    @retval 1 : runnable
*/
int stc_rng_insert(void *obj_ptr,uint8_t opcode, fid_t fid,uint64_t bid,uint64_t nb_blks,int *entry_idx_p);
/*
**____________________________________________________________
*/
/**
*  remove an entry from the ring

   removing the entry consist in clearing the object pointer associated with the 
   entry to remove.
   If there is a new runnable request, the service returns the pointer and the opcode
   associated with that entry
   
   @param entry_idx : index of the ring entry to remove

   @retval 0 on success
   @retval < 0 on error
*/
int stc_rng_release_entry(int entry_idx, void **next_running_p,uint8_t *opcode_p);


static inline int stc_rng_is_full() {
    uint16_t next_wr_idx = (stc_rng_wr_idx + 1) % STORCLI_RING_SZ;
    if (next_wr_idx == stc_rng_rd_idx)
        return 1;
    return 0;
}

#endif
