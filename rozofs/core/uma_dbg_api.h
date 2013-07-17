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
#ifndef UMA_DBG_API_H
#define UMA_DBG_API_H

#include <rozofs/common/types.h>

#include "ruc_common.h"


/*
** Max length of the user payload when answering to a debug command
*/
#define UMA_DBG_MAX_SEND_SIZE (1024*32)


/*
   The function uma_dbg_addTopic enables to declare a new topic to
   the debug module. You have to give :

   1) topic :
   The topic is a string that has to be unic among the list of all
   topics knwon by the debug module. If you give a topic name that
   already exist, the debug module process an ERRLOG. So change
   the topic names.

   2) funct :
   A function with a specific prototype that will be called when
   the debug module receives a command beginning with the
   corresponding topic name.


   For instance : you declare a topic named "capicone"
   uma_dbg_addTopic("capicone", capicone_debug_entry);

   When on debug session is entered the following command
   line
     capicone     234 0x56    lulu         0o0
   the function capicone_debug_entry is called with

   - argv[] :
     argv[0] = "capicone"
     argv[1] = "234"
     argv[2] = "0x56"
     argv[3] = "lulu"
     argv[4] = "0o0"
     argv[5]...argv[40] = NULL

   - tcpRef : the reference of the debug session that the command
              comes from

   - bufRef : a reference a buffer in which  Y O U   H A V E  to
     put the response (note that the buffer is 2048 bytes length)

   To send a response, you are given an API uma_dbg_send which
   prototype is a bit like printf :
   - tcpCnxRef : is the reference of the debug session
   - bufRef : is the buffer for the response
   - end : is true when your response is complete, and false
           when a extra response buffer is to be sent. It is
	   up to you to get an other buffer.
   Examples :
   If you are happy
     uma_dbg_send(tcpCnxRef,bufRef,TRUE,"I am happy");
   or if you think that argv[1 has a bad value
     uma_dbg_send(tcpCnxRef,bufRef,TRUE,"2nd parameter has a bad value \"%s\"", argv[1]);
   or
     uma_dbg_send(tcpCnxRef,bufRef,TRUE,"IP addres is %u.%u.%u.%u",
                  ip>>24 & 0xFF, ip>>16 & 0xFF, ip>>8 & 0xFF, ip & 0xFF);

 */
 //64BITS typedef void (*uma_dbg_topic_function_t)(char * argv[], uint32_t tcpRef, uint32 bufRef);
typedef void (*uma_dbg_topic_function_t)(char * argv[], uint32_t tcpRef, void *bufRef);
void uma_dbg_addTopic(char * topic, uma_dbg_topic_function_t funct);
void uma_dbg_init(uint32_t nbElements, uint32_t ipAddr, uint16_t serverPort) ;
//64BITS void uma_dbg_send(uint32_t tcpCnxRef, uint32 bufRef, uint8_t end, char *fmt, ... );
void uma_dbg_send(uint32_t tcpCnxRef, void *bufRef, uint8_t end, char *fmt, ... ); 
void uma_dbg_set_name( char * system_name) ;

//64BITS typedef uint32_t (*uma_dbg_catcher_function_t)(uint32 tcpRef, uint32 bufRef);
typedef uint32_t (*uma_dbg_catcher_function_t)(uint32_t tcpRef, void *bufRef);

//64BITS uint32_t uma_dbg_catcher_DFT(uint32 tcpRef, uint32 bufRef);
uint32_t uma_dbg_catcher_DFT(uint32_t tcpRef, void *bufRef);
void uma_dbg_setCatcher(uma_dbg_catcher_function_t funct);
#endif
