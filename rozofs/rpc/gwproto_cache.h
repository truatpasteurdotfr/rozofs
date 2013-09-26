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
 
#ifndef EXPGW_PROTO_CACHE_H
#define EXPGW_PROTO_CACHE_H


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/


/*
** One section header in the message to invalidate some sections
** from the exportd to the export gateway
*/
typedef union _rozofs_section_header_u {

  uint64_t    u64;
  
  struct {
    uint64_t    absolute_idx:16;  /**< Starting index of the section */ 
    uint64_t    section_size:5;   /**< Size in byte of the section   */ 
    uint64_t    filler:11;
    uint64_t    byte_bitmap:32;   /**< presence bitmap of the bytes  */     
  } field;
  
} rozofs_section_header_u;


#ifdef __cplusplus
}
#endif /*__cplusplus */


#endif


