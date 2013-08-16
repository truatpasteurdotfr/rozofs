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
 
 #ifndef STORCLI_LBG_PROTOTYPES_H
 #define STORCLI_LBG_PROTOTYPES_H
 
 #include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/common/xmalloc.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <string.h>
#include <rpc/pmap_clnt.h>
#include <unistd.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
#include <rozofs/rpc/eclient.h>
#include "rpcclt.h"
 
#define STORCLI_PER_FSMOUNT_POWER2 1
#define STORCLI_PER_FSMOUNT (1<<STORCLI_PER_FSMOUNT_POWER2) 
  
 
 int export_lbg_initialize(exportclt_t *exportclt ,unsigned long prog,
        unsigned long vers,uint32_t port_num);
        
int storcli_get_storcli_idx_from_fid(fid_t fid);
       
//int storaged_lbg_initialize(mstorage_t *s);
int storcli_lbg_initialize(exportclt_t *exportclt ,uint16_t rozofsmount_instance,int first_instance,int nb_instances);
int storcli_lbg_get_lbg_from_fid(fid_t fid);
int storcli_lbg_get_load_balancing_reference(int idx);
 #endif
