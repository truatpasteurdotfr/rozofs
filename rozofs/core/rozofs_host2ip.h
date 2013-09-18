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
 
 #ifndef ROZOFS_HOST2IP_H
#define ROZOFS_HOST2IP_H

#include <string.h>
#include <netdb.h>
#include <rozofs/common/log.h>


#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/*
**______________________________________________________________________________
*/
/**
*  Convert a hostname into an IP v4 address in host format 

@param host : hostname
@param ipaddr_p : return IP V4 address arreay

@retval 0 on success
@retval -1 on error (see errno faor details
*/
static inline int rozofs_host2ip(char *host, uint32_t *ipaddr_p) {
  struct hostent *hp;    
  /*
  ** get the IP address of the storage node
  */
  if ((hp = gethostbyname(host)) == 0) {
    severe("gethostbyname failed for host : %s, %s", host, strerror(errno));
    return -1;
  }
  if (hp->h_length != 4) {
    severe("rozofs_host2ip address length is %d", hp->h_length);
    return -1;
  }  
  bcopy((char *) hp->h_addr, (char *) ipaddr_p, hp->h_length);
  *ipaddr_p = ntohl(*ipaddr_p);
  return 0;
}

#ifdef __cplusplus
}
#endif /*__cplusplus */




#endif

