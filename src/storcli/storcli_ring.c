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
#include <stdint.h>
#include <stdio.h>
#include <uuid/uuid.h>
#include <stdlib.h>
#include <string.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include "config.h"
#include "storcli_ring.h"
 #include <uuid/uuid.h>

uint64_t stc_rng_collision_count;
uint64_t stc_rng_full_count;
uint64_t stc_rng_hash_collision_count;
uint64_t stc_rng_submit_count;
uint64_t stc_rng_parallel_count;
int      stc_rng_serialize;
int      stc_rng_max_count; 

stc_rng_hash_entry_t *stc_ring_tb_p[STC_RNG_HASH_SZ];

/*
**  Lists API
*/
/*
**__________________________________________________________________
*/
static inline void stc_rng_listEltInit(stc_rng_list_t *p)
{
  p->ps = p;
  p->pp =p;
}
/*
**__________________________________________________________________
*/
/**
*  Insert Head

   @param phead : head of the list
   @param elem: element to insert
*/
static inline void stc_rng_objInsert(stc_rng_list_t *phead,stc_rng_list_t *pobj)
{
   /*
   **  control on header and object
   */
   pobj->ps = phead->ps;
   phead->ps->pp = pobj;
   phead->ps = pobj;
   pobj->pp = phead;
}
/*
**__________________________________________________________________
*/
/**
*  Insert tail

   @param phead : head of the list
   @param elem: element to insert
*/
static inline void stc_rng_objInsertTail(stc_rng_list_t *phead,stc_rng_list_t *pobj)
{

   pobj->ps = phead;
   phead->pp->ps = pobj;
   pobj->pp = phead->pp;
   phead->pp = pobj;
}


/*
**__________________________________________________________________
*/
/**
*  remove from the list

   @param elem: element to insert
*/

static inline void stc_rng_objRemove(stc_rng_list_t *pobj)
{

  pobj->pp->ps = pobj->ps;
  pobj->ps->pp = pobj->pp;
  /*
  ** make it unlinked by setting ps=pp=pobj
  */
  pobj->ps = pobj;
  pobj->pp = pobj;
}

/*
**__________________________________________________________________
*/
/**
*  check empty list

   @param phead: head of the list
   
   @retval 1 if empty
   @retval 0 if not empty
*/
static inline uint32_t ruc_objIsEmptyList(stc_rng_list_t *phead)
{
   if (phead->ps == phead)
      return 1;
   return 0;
}

/*
**__________________________________________________________________
*/
/**
*  Get Next element from list

   @param phead: head of the list
   @param pstart: start pointer, NULL for beginning of list
   
   @retval next element or NULL
*/
static inline stc_rng_list_t *stc_rng_objGetNext(stc_rng_list_t *phead,
                                 stc_rng_list_t **pstart)
{
   stc_rng_list_t *pnext;
   
   if (*pstart == (stc_rng_list_t*)NULL)
      pnext = phead->ps;
   else
      pnext = *pstart;
   if (pnext == phead)
      return (stc_rng_list_t *) NULL;
   *pstart = pnext->ps;
   return pnext;
}



void stc_rng_print()
{
    int j;
    int i;
    stc_rng_hash_entry_t *p;
    
    for (j = 0; j < STC_RNG_HASH_SZ; j++)
    {
      printf("Hash entry %d\n",j);
      p = stc_ring_tb_p[j] ;
      for (i = 0; i < STC_RNG_HASH_ENTRIES_WORD; i++)
      {
	printf("  bitmap[%d]=%8.8x\n",i,p->hash_bit[i]);
      }
      printf("\n");
    }
    printf("stc_rng_hash_collision_count %llu\n",(long long unsigned int)stc_rng_hash_collision_count);
    printf("stc_rng_submit_count         %llu\n",(long long unsigned int)stc_rng_submit_count);
    printf("stc_rng_parallel_count       %llu\n",(long long unsigned int)stc_rng_parallel_count);
    printf("stc_rng_full_count           %llu\n",(long long unsigned int)stc_rng_full_count);     
}
/*
**__________________________________________________________________
*/
/**
*  Init of the storcli command ring buffer

  @param none 
  
  @retval :0 on success
  @retval :-1 on error (see errno for details)
*/
int stc_rng_init()
{
    int j;
    int i;
    stc_rng_list_t *list_p;
    
    memset(stc_ring_tb_p,0,sizeof(stc_rng_hash_entry_t*)*STC_RNG_HASH_SZ);
    
    for (j = 0; j < STC_RNG_HASH_SZ; j++)
    {
      stc_ring_tb_p[j] = malloc(sizeof(stc_rng_hash_entry_t));
      if (stc_ring_tb_p[j] == NULL) return -1;

      memset(stc_ring_tb_p[j],0,sizeof(stc_rng_hash_entry_t));

      list_p = &stc_ring_tb_p[j]->list_table[0];
      for (i = 0; i < STC_RNG_HASH_ENTRIES; i++)
      {
	list_p->ps = list_p;
	list_p->pp = list_p;
	list_p++;
      }
    }
    stc_rng_collision_count = 0;
    stc_rng_submit_count = 0;
    stc_rng_serialize = 0;
    stc_rng_parallel_count = 0;
    stc_rng_hash_collision_count = 0;
    stc_rng_full_count = 0;
    stc_rng_max_count = 0;
    
    return 0;
}


/*
**______________________________________________________________________________
*/
/**
*  hash computation from parent fid and filename (directory name or link name)

  @param h : initial hash value
  @param key2:  fid
  
  @retval hash value
*/
static inline unsigned int fid_hash(void *key) {
    uint32_t hash = 0;
    uint8_t *c;
    for (c = key+15; c != key + 16; c++)
    {
        hash = *c + (hash << 6) + (hash << 16) - hash;
//	printf("hash %x\n",hash);
    }
	
    return hash;
}

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
int stc_rng_insert(void *obj_ptr,uint8_t opcode, fid_t fid,uint64_t bid,uint64_t nb_blks,int *entry_idx_p)
{
    uint32_t hash;
    uint32_t rng_tb_idx;
    stc_rng_hash_entry_t *ring_tb_p;
    int i,j;
    uint32_t val32;
    uint32_t bit;
    int match;
    int cur_entry_id = 0;
    int found_idx = -1;
    int runnable = 1;
    stc_rng_entry_t *entry_p = NULL;
    stc_rng_entry_t *elt;
    stc_rng_list_t *pnext=NULL;
    int found = 0;
    stc_rng_list_t *head_p;

   stc_rng_submit_count++;
    /*
    ** Get the ring entry from obj_ptr
    */
    entry_p = stc_rng_get_entry_from_obj_ctx(obj_ptr);
    /*
    ** init of the entry
    */
    stc_rng_listEltInit(&entry_p->list);
    entry_p->bid = bid;
    entry_p->nb_blks = nb_blks;
    entry_p->obj_ptr = obj_ptr;
    entry_p->opcode = opcode;
    entry_p->state = 1;
    /*
    ** Get the hash entry
    */
    hash = fid_hash(fid);
    rng_tb_idx =  hash % STC_RNG_HASH_SZ;
    ring_tb_p = stc_ring_tb_p[rng_tb_idx];
    /*
    ** search among the allocated entries
    */
    for (i = 0; i < STC_RNG_HASH_ENTRIES_WORD; i++)
    {
      val32 = ring_tb_p->hash_bit[i];
      while(val32 != 0)
      {
	bit = __builtin_ffs(val32);
	match = memcmp( ring_tb_p->fid_table[cur_entry_id+bit-1],fid,sizeof(fid_t));
	if (match != 0)
	{
	  stc_rng_hash_collision_count++;
	  val32 &=(~(1<<(bit-1)));
	  continue;
	}
	/**
	*  found
	*/
	found_idx = cur_entry_id+bit-1 ;
	break;	
      }
      if (found_idx != -1) break;
      cur_entry_id +=32;      
    }
    if (found_idx == -1)
    {
      /*
      ** search for a free entry
      */
      cur_entry_id = 0;
      for (i = 0; i < STC_RNG_HASH_ENTRIES_WORD; i++)
      {
	if (found_idx != -1) break;
	val32 = ring_tb_p->hash_bit[i];
	for ( j= 0; j < 32; j++)
	{
           if (( (1 <<j) & val32) == 0)
	   {
	     found_idx = cur_entry_id + j;
	     ring_tb_p->hash_bit[i] |= 1<<j;
	     /*
	     ** store the fid and init of the linked list
	     */
             memcpy( ring_tb_p->fid_table[found_idx],fid,sizeof(fid_t));
             stc_rng_listEltInit(&ring_tb_p->list_table[found_idx]);
	     break;	 
	   }      
	} 
	cur_entry_id +=32;      
      }
    }
    if (found_idx ==-1)
    {
       stc_rng_full_count++;
       return -1;
    }
    /*
    ** now evaluate if we can engage the transaction
    */
    found = 0;
    head_p = &ring_tb_p->list_table[found_idx]; 
    while((elt = (stc_rng_entry_t*)stc_rng_objGetNext(head_p,&pnext))!=NULL)
    {
      if (stc_rng_serialize)
      {
        runnable = 0;
	break;
      }    
      /*
      ** we have the same FID check the block offset and the number of blocks
      */
      if ((bid+nb_blks <= elt->bid) || (bid >= elt->bid+elt->nb_blks))
      {
        /*
	** no collision -> check the next entry
	*/
	found++;
	continue;
      }
      /*
      ** there is a collision
      */
      found = 0;
      stc_rng_collision_count++;
      runnable = 0;
      break;    
    }
    /*
    **  insert the entry
    */
    if (found) stc_rng_parallel_count++;
    stc_rng_objInsertTail(head_p,&entry_p->list);    
    entry_p->state = runnable;
    /*
    ** return the entry index
    */
    *entry_idx_p = (rng_tb_idx & 0xff) | (found_idx << 8);    
    return runnable;

}


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
int stc_rng_release_entry(int entry_idx, void **next_running_p,
        uint8_t *opcode_p,stc_rng_entry_t *cur_p) 
{

    *next_running_p = NULL;
    int rng_tb_idx;
    int found_idx;
    stc_rng_entry_t *elt;
    stc_rng_list_t *pnext=NULL;
    stc_rng_list_t *head_p;
    stc_rng_hash_entry_t *ring_tb_p;

    /*
    ** split the entry idx
    */
    rng_tb_idx = entry_idx & 0xff;
    found_idx  = entry_idx>>8;
    
    ring_tb_p = stc_ring_tb_p[rng_tb_idx];
    head_p = &ring_tb_p->list_table[found_idx]; 
    while (head_p->ps == &cur_p->list)
    {
       stc_rng_objRemove(&cur_p->list);
       /*
       ** it the first of the list, check if the next entry is waiting
       */
       elt = (stc_rng_entry_t*)stc_rng_objGetNext(head_p,&pnext);
       if (elt==NULL) break; 
       if (elt->state ==  STORCLI_RUN) break;     
       /*
        ** the entry is waiting: schedule it
       */
       *next_running_p = elt->obj_ptr;
       elt->state = STORCLI_RUN;
       *opcode_p = elt->opcode; 
       break;   
    }
    /*
    ** release the entry
    */
    stc_rng_objRemove(&cur_p->list);
    if (ruc_objIsEmptyList(head_p))
    {
      uint8_t bitmap_idx = found_idx/32;
      uint8_t bit_idx = found_idx%32;
      ring_tb_p->hash_bit[bitmap_idx] &= (~(1<< bit_idx));  
    }  
    return 0;
}


