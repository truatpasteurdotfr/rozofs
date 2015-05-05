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

#include "common_config.h"
#include <rozofs/core/uma_dbg_api.h>


static char config_file_name[256] = {0};
static int  config_file_is_read=0;
common_config_t common_config;

#define COMMON_CONFIG_SHOW_BOOL(val)  {\
  pChar += rozofs_string_padded_append(pChar, 24, rozofs_left_alignment, #val);\
  if (common_config.val) pChar += rozofs_string_append(pChar, ": True  ");\
  else                   pChar += rozofs_string_append(pChar, ": False ");\
  if (rozofs_default_##val) pChar += rozofs_string_append(pChar, "\t(True)\n");\
  else                      pChar += rozofs_string_append(pChar, "\t(False)\n");\
}

#define COMMON_CONFIG_SHOW_INT(val)  {\
  pChar += rozofs_string_padded_append(pChar, 24, rozofs_left_alignment, #val);\
  pChar += rozofs_string_append(pChar, ": ");\
  pChar += rozofs_i32_append(pChar, common_config.val);\
  pChar += rozofs_string_append(pChar," \t(");\
  pChar += rozofs_i32_append(pChar, rozofs_default_##val);\
  *pChar++ = ' ';\
  *pChar++ = '[';\
  pChar += rozofs_i32_append(pChar, rozofs_min_##val);\
  *pChar++ = ':';\
  pChar += rozofs_i32_append(pChar, rozofs_max_##val);\
  *pChar++ = ']';\
  *pChar++ = ')';\
  pChar += rozofs_eol(pChar);\
}  
void show_common_config(char * argv[], uint32_t tcpRef, void *bufRef) {
  char *pChar = uma_dbg_get_buffer();

  if (config_file_is_read==0) {
    pChar += rozofs_string_append(pChar,"Can not read configuration file ");    
  }
  pChar += rozofs_string_append(pChar,config_file_name);
  pChar += rozofs_eol(pChar);

  COMMON_CONFIG_SHOW_INT(nb_core_file);
  COMMON_CONFIG_SHOW_INT(nb_disk_thread); 
  COMMON_CONFIG_SHOW_BOOL(storio_multiple_mode); 
  COMMON_CONFIG_SHOW_BOOL(crc32c_check);
  COMMON_CONFIG_SHOW_BOOL(crc32c_generate);
  COMMON_CONFIG_SHOW_BOOL(crc32c_hw_forced);
  
  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
  return;          
}









int  boolval;  
#define COMMON_CONFIG_READ_BOOL(val)  {\
  common_config.val = rozofs_default_##val;\
  if (config_lookup_bool(&cfg, #val, &boolval)) { \
    common_config.val = boolval;\
  }\
}  


#if (((LIBCONFIG_VER_MAJOR == 1) && (LIBCONFIG_VER_MINOR >= 4)) \
             || (LIBCONFIG_VER_MAJOR > 1))
int               intval;
#else
long int          intval;
#endif

#define COMMON_CONFIG_READ_INT(val)  {\
  common_config.val = rozofs_default_##val;\
  if (config_lookup_int(&cfg, #val, &intval)) { \
    if (intval<rozofs_min_##val) {\
      common_config.val = rozofs_min_##val;\
    }\
    else if (intval>rozofs_max_##val) { \
      common_config.val = rozofs_max_##val;\
    }\
    else {\
      common_config.val = intval;\
    }\
  }\
} 

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
void common_config_read(char * fname) {
  config_t          cfg; 
#if (((LIBCONFIG_VER_MAJOR == 1) && (LIBCONFIG_VER_MINOR >= 4)) \
             || (LIBCONFIG_VER_MAJOR > 1))
  int               intval;
#else
  long int          intval;
#endif      

  uma_dbg_addTopic("cconf",show_common_config);
  
  /*
  ** Initialize lib config working structure
  */
  config_init(&cfg);

  /*
  ** Read the file
  */
  if (fname == NULL) {
    strcpy(config_file_name,ROZOFS_DEFAULT_COMMON_CONF);
  }
  else {
    strcpy(config_file_name,fname);    
  }  

  config_file_is_read = 1;
  if (config_read_file(&cfg, config_file_name) == CONFIG_FALSE) {
    errno = EIO;
    severe("can't read %s: %s (line %d).", config_file_name, config_error_text(&cfg),
            config_error_line(&cfg));
    config_file_is_read = 0;	    
  }

  /*
  ** Look up for number of core files
  */  
  COMMON_CONFIG_READ_INT(nb_core_file);

  /*
  ** Look up for number of disk threads
  */  
  COMMON_CONFIG_READ_INT(nb_disk_thread);
  
  /*
  ** Is storio in multiple or single mode
  */   
  COMMON_CONFIG_READ_BOOL(storio_multiple_mode);


  /*
  ** CRC32
  */
  COMMON_CONFIG_READ_BOOL(crc32c_check);
  COMMON_CONFIG_READ_BOOL(crc32c_generate);
  COMMON_CONFIG_READ_BOOL(crc32c_hw_forced);
    
  /*
  ** Free lib config working structure
  */
  config_destroy(&cfg);
}
