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
 
#ifndef ROZOFS_HOST_LIST_H
#define ROZOFS_HOST_LIST_H

#include <string.h>
#include <stdio.h>
#include <netdb.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

#define ROZOFS_HOST_LIST_MAX_HOST 16
extern char * rozofs_host_pointer[];
extern int    rozofs_host_nb;

/*__________________________________________________________________________
*/
/**
*  Builld an array of pointers to hosts from a string containing
*  hosts separated by a specific character.
*
* @param host_list  string containing a list of hosts
* @param separator  the character that separates a host from an other in
*                   the list
*
* @retval the number of host in the host list
*/
int rozofs_host_list_parse(const char * host_list, char separator) ;


/*__________________________________________________________________________
*/
/**
*  Get the number of host in the host list
*
* @retval the number of host in the host list
*/
static inline int rozofs_host_list_get_number() {
  return rozofs_host_nb;
}
/*__________________________________________________________________________
*/
/**
*  Get a host from the host list
*  
* @param the rank of the host in the list starting from 0
*
* @retval the host name
*/
static inline char * rozofs_host_list_get_host(int nb) {
  if (nb >= rozofs_host_nb) return NULL;
  return rozofs_host_pointer[nb];
}

#ifdef __cplusplus
}
#endif /*__cplusplus */

#endif

