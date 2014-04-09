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

#include <inttypes.h>
#include "rozofs_fuse.h"
#include "rozofs_fuse_api.h"
#include "rozofsmount.h"
#include "rozofs_xattr_flt.h"

/*
** Global data
*/

int rozofs_xattr_flt_count; /**< current number of filters */
int rozofs_xattr_flt_enable; /**< assert to 1 to enbale the filter */
rozofs_xattr_flt_t rozofs_xattr_flt_filter[ROZOFS_XATTR_FILTR_MAX];

/*
**______________________________________________________________________________
*/
/**
*   search an extended attribute id in the filter;
*
    @param name : extended attribute id
    
    @retval 0 on success
    @retval -1 no found
*/
int rozofs_xattr_flt_search(char *name)
{
  int i;
  rozofs_xattr_flt_t *p;
  for (i = 0; i < rozofs_xattr_flt_count; i++)
  {
    p= &rozofs_xattr_flt_filter[i];
    if (p->buffer == NULL) continue;   
    if (strcmp(name,p->buffer)==0) return 0;
  } 
  return -1; 
}
/*
**______________________________________________________________________________
*/
/**
*   insert an extended attribute id in the filter;
*
    @param name : extended attribute id
    
    @retval 0 on success
    @retval -1 on error
*/
int rozofs_xattr_flt_insert(char *name)
{
  int i;
  rozofs_xattr_flt_t *p;
  int len;
  /*
  ** search if the entry already exists
  */
  if (rozofs_xattr_flt_search(name) == 0) return 0;
  /*
  ** check full condition
  */
  if (rozofs_xattr_flt_count == ROZOFS_XATTR_FILTR_MAX) return -1;
  /*
  ** search for a free entry
  */
  for (i = 0; i < ROZOFS_XATTR_FILTR_MAX; i++)
  {
    p= &rozofs_xattr_flt_filter[i];
    if (p->status == 0)
    {
      len = strlen(name)+1;
      p->buffer = malloc(len);
      if (p->buffer == NULL) return -1;
      strcpy(p->buffer,name);
      p->status = 1;  
      rozofs_xattr_flt_count++;
      return 0;  
    }   
  } 
  return -1; 
}
/*
**______________________________________________________________________________
*/
/**
*   remove an extended attribute id in the filter;
*
    @param name : extended attribute id
    
    @retval 0 on success
    @retval -1 on error
*/
int rozofs_xattr_flt_remove(char *name)
{
  int i;
  rozofs_xattr_flt_t *p;
  for (i = 0; i < rozofs_xattr_flt_count; i++)
  {
    p= &rozofs_xattr_flt_filter[i];
    if (p->buffer == NULL) continue;   
    if (strcmp(name,p->buffer)!=0) continue;
    /*
    ** found , so remove it
    */
    free(p->buffer);
    p->status =0;
    p->buffer = NULL;
    rozofs_xattr_flt_count--;
    return 0;
  } 
  return -1; 
}
/*
**______________________________________________________________________________
*/
void rozofs_xattr_flt_status(char *pChar)
{
   pChar+=sprintf(pChar,"xattribute filter state: %s\n",(rozofs_xattr_flt_enable==0)?"Disabled":"Enabled");
   pChar+=sprintf(pChar,"number of filters      : %d/%d\n",rozofs_xattr_flt_count,ROZOFS_XATTR_FILTR_MAX);

}


/*
**______________________________________________________________________________
*/
void show_xattr_flt_buffer(char *pChar)
{
   int i;
   rozofs_xattr_flt_t *p;
   
   pChar+=sprintf(pChar,"xattribute filter state: %s\n",(rozofs_xattr_flt_enable==0)?"Disabled":"Enabled");
   pChar+=sprintf(pChar,"number of filters      : %d/%d\n",rozofs_xattr_flt_count,ROZOFS_XATTR_FILTR_MAX);

   for (i = 0; i < ROZOFS_XATTR_FILTR_MAX; i++)
   {
     p= &rozofs_xattr_flt_filter[i];
     if (p->buffer == NULL) continue;;
     pChar+=sprintf(pChar,"%s\n",p->buffer);
   } 
}
/*
**______________________________________________________________________________
*/

static char * show_xattr_flt_help(char * pChar) {
  pChar += sprintf(pChar,"usage:\n");
  pChar += sprintf(pChar,"xattr_flt enable        : enable xattributes filter\n");  
  pChar += sprintf(pChar,"xattr_flt disable       : disable xattributes filter\n");  
  pChar += sprintf(pChar,"xattr_flt add <value>   : add xattribute key from filter\n");  
  pChar += sprintf(pChar,"xattr_flt del <value>   : delete xattribute key from filter\n");  
  pChar += sprintf(pChar,"xattr_flt status        : current status of the xttribute filter\n");  
  pChar += sprintf(pChar,"xattr_flt               : display the content of the xattribute filter\n");  
  return pChar; 
}  


void show_xattr_flt(char * argv[], uint32_t tcpRef, void *bufRef) {
    char *pChar = uma_dbg_get_buffer();
    int   err;   
     
    if (argv[1] != NULL)
    {
        if (strcmp(argv[1],"enable")==0) {
	  if (rozofs_xattr_flt_enable != 1)
	  {
            rozofs_xattr_flt_enable = 1;
            uma_dbg_send(tcpRef, bufRef, TRUE, "xattribute filter is now enabled\n");    
	  }
	  else
	  {
            uma_dbg_send(tcpRef, bufRef, TRUE, "xattribute filter is already enabled\n");    
	  }
	  return;
	}  
        if (strcmp(argv[1],"status")==0) {
	  rozofs_xattr_flt_status(pChar);
	  uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
	  return;
	}  
        if (strcmp(argv[1],"add")==0) {
	  if (argv[2] == NULL)
	  {
            pChar += sprintf(pChar, "argument is missing\n");
	    pChar = show_xattr_flt_help(pChar);
	    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
	    return;	  	  
	  }
	  err = rozofs_xattr_flt_insert(argv[2]);
	  if (err < 0) {
            pChar += sprintf(pChar, "cannot insert %s\n",argv[2]);
	    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
	    return;
	  }
          uma_dbg_send(tcpRef, bufRef, TRUE, "Done!!\n");
	  return;
	}  
        if (strcmp(argv[1],"del")==0) {
	  if (argv[2] == NULL)
	  {
            pChar += sprintf(pChar, "argument is missing\n");
	    pChar = show_xattr_flt_help(pChar);
	    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
	    return;	  	  
	  }
	  err = rozofs_xattr_flt_remove(argv[2]);
	  if (err < 0) {
            pChar += sprintf(pChar, "%s not found\n",argv[2]);
	    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
	    return;
	  }
          uma_dbg_send(tcpRef, bufRef, TRUE, "Done!!\n");
	  return;
	}  

        if (strcmp(argv[1],"disable")==0) {
	  if (rozofs_xattr_flt_enable != 0)
	  {
            rozofs_xattr_flt_enable = 0;
            uma_dbg_send(tcpRef, bufRef, TRUE, "xattribute filter is now disabled\n");    
	  }
	  else
	  {
            uma_dbg_send(tcpRef, bufRef, TRUE, "xattribute filter is already disabled\n");    
	  }
	  return;
        }
	pChar = show_xattr_flt_help(pChar);
	uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
	return;   	
    }
    show_xattr_flt_buffer(pChar);
    uma_dbg_send(tcpRef, bufRef, TRUE, uma_dbg_get_buffer());
 }




/*
**______________________________________________________________________________
*/
void rozofs_xattr_flt_filter_init()
{
  int i;
  for (i = 0; i < ROZOFS_XATTR_FILTR_MAX; i++)
  {
    rozofs_xattr_flt_filter[i].buffer = NULL;  
    rozofs_xattr_flt_filter[i].status = 0;  
  
  }
  rozofs_xattr_flt_count = 0;  
  rozofs_xattr_flt_enable = 0;
  rozofs_xattr_flt_insert("security.capability");
}

