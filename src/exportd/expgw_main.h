/*
  Copyright (c) 2010 Fizians SAS. <http://www.fizians.com>
  This file is part of Rozofs.

  Rozofs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 3 of the License,
  or (at your option) any later version.

  Rozofs is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see
  <http://www.gnu.org/licenses/>.
 */
 
 #ifndef EXPGW_MAIN_H
#define EXPGW_MAIN_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/


#include "expgw_fid_cache.h"
#include "expgw_attr_cache.h"

/**
*  Init of the data structure used for the non blocking entity
  
  @param dbg_port: debug listening port
  @param local_ip_addr:IP debug address
  
  @retval 0 on success
  @retval -1 on error
*/
int expgw_non_blocking_init(uint16_t dbg_port, uint32_t local_ip_addr);


/*
**____________________________________________________
*/
/**
   

  Creation of the north interface for rozofsmount (AF_INET)

@param     : src_ipaddr_host : source IP address in host format
@param     : src_port_host : port in host format
@param     : read_write_buf_count : number of read/write buffer
@param     : read_write_buf_sz : size of a read/write buffer

@retval   : RUC_OK : done
@retval          RUC_NOK : out of memory
*/

int expgw_exportd_north_interface_init(uint32_t src_ipaddr_host,uint16_t src_port_host,
                             int read_write_buf_count,int read_write_buf_sz);


#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif
