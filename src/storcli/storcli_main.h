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
 
#ifndef ROZOFS_STORCLI_CNF_MGT_H
#define ROZOFS_STORCLI_CNF_MGT_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <rozofs/rozofs.h>
#include <rozofs/rpc/eclient.h>

 
extern uint32_t  *rozofs_storcli_cid_table[];
 
 #define FAKE_CID_FDL 1
 
 
 /**
 * init of the cid table. That table contains pointers to
 * a sid/lbg_id association table, the primary key being the sid index
 */
  void rozofs_storcli_cid_table_init();

/**
*  insert an entry in the  rozofs_storcli_cid_table table

   @param cid : cluster index
   @param sid : storage index
   @param lbg_id : load balancing group index
   
   @retval 0 on success
   @retval < 0 on error
*/
int rozofs_storcli_cid_table_insert(cid_t cid,sid_t sid,uint32_t lbg_id);


/**
*  Get the load balancing group that is associated with the cid/sid

   @param cid : cluster index
   @param sid : storage index
   
   @retval >=0 reference of the load balancing group
   @retval < 0 no load balancing group
*/
static inline int rozofs_storcli_get_lbg_for_sid(cid_t cid,sid_t sid)
{
  uint32_t *sid_lbg_id_p;

  if (cid >= ROZOFS_CLUSTERS_MAX)
  { 
    /*
    ** out of range
    */
    return -1;
  }

  
  sid_lbg_id_p = rozofs_storcli_cid_table[cid-1];
  if (sid_lbg_id_p == NULL)
  {
    return -1;
  }
  return (int) sid_lbg_id_p[sid-1];
}



/*
**____________________________________________________
*/
/**
   rozofs_storcli_module_init

  create the Transaction context pool

@param     : read_write_buf_count : number of read/write buffer
@param     : read_write_buf_sz : size of a read/write buffer
@param     : eid : unique identifier of the export to which the storcli process is associated
@param     : rozofsmount_instance : instance number is needed when several reozfsmount runni ng oin the same share the export


@retval   : RUC_OK : done
@retval          RUC_NOK : out of memory
*/

int rozofs_storcli_north_interface_init(uint32_t eid,uint16_t rozofsmount_instance,uint32_t instance,
                             int read_write_buf_count,int read_write_buf_sz);

/**
   rozofs_storcli_module_init

  create the Transaction context pool for data read/write


@retval   : RUC_OK : done
@retval          RUC_NOK : out of memory
*/
uint32_t rozofs_storcli_module_init();

/*__________________________________________________________________________
 */
/** Thread : Check if the connections for one storage node are active or not
 *
 * @param storage: the storage node
 */

void *connect_storage(void *v);
/*__________________________________________________________________________
 */
/**
* Init of the load balancing group from mstorage configuration
*/
int storaged_lbg_initialize(mstorage_t *s, int index);
/**______________________________________________________________________________
*/
/**
*  get the current site number of the rozofsmount client

*/
extern int storcli_site_number;
static inline int storcli_get_site_number()
{
  return storcli_site_number;
}
/*__________________________________________________________________________
** Create every LBG toward a storage node while its configuration is received
** from the storaged
 */
int rozofs_storcli_setup_all_lbg_of_storage(mstorage_t *s);
/**
* get the owner of the storcli

  @retval : pointer to the owner
*/
char *storcli_get_owner();
#endif
