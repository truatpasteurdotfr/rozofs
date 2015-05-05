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

#ifndef _COMMON_CONFIG_H
#define _COMMON_CONFIG_H

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <libconfig.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>

#include <rozofs/rozofs.h>
#include <rozofs/common/log.h>
/*
** Default configuration file name
*/
#define ROZOFS_DEFAULT_COMMON_CONF "/etc/rozofs/common.conf"

/*
** Default parameter values
*/
#define rozofs_default_nb_core_file             1
#define rozofs_min_nb_core_file                 0
#define rozofs_max_nb_core_file                 4

#define rozofs_default_nb_disk_thread           3
#define rozofs_min_nb_disk_thread               2
#define rozofs_max_nb_disk_thread              16

#define rozofs_default_storio_multiple_mode     TRUE
#define rozofs_default_crc32c_check             TRUE
#define rozofs_default_crc32c_generate          TRUE
#define rozofs_default_crc32c_hw_forced         FALSE

/*
** Common configuration parameters
*/
typedef struct _common_config_t {
  uint32_t    nb_core_file;
  uint32_t    nb_disk_thread; 
  uint32_t    storio_multiple_mode; 
  uint32_t    crc32c_check;
  uint32_t    crc32c_generate;
  uint32_t    crc32c_hw_forced;  
} common_config_t;
  
extern common_config_t common_config;
  
/*
** Read the configuration file and initialize the configuration 
** global variable.
**
** @param file : the name of the configuration file
**               when NULL the default configuration is read
**
** retval The address of the glocal vriable containing the 
** common configuration
*/
void common_config_read(char * file);

#endif
