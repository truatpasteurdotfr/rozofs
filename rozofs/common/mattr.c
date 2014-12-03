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

#include <string.h>

#include "mattr.h"
#include "xmalloc.h"

void mattr_initialize(mattr_t *mattr) {
    check_memory(mattr);

    //mattr->fid XXX fid not initialized.
    mattr->cid = UINT16_MAX;
    memset(mattr->sids, 0, ROZOFS_SAFE_MAX);
}

void mattr_release(mattr_t *mattr) {
    check_memory(mattr);

    //mattr->fid XXX fid not initialized.
    mattr->cid = UINT16_MAX;
    memset(mattr->sids, 0, ROZOFS_SAFE_MAX);
}

/*
**__________________________________________________________________
*/
/**
* store the file name in the inode
  The way the name is stored depends on the size of
  the filename: when the name is less than 62 bytes
  it is directly stored in the inode
  
  @param inode_fname_p: pointer to the array used for storing object name
  @param name: name of the object
  @param dentry_fname_info_p :pointer to the array corresponding to the fname in dentry
*/
void exp_store_fname_in_inode(rozofs_inode_fname_t *inode_fname_p,
                              char *name,
			      mdirent_fid_name_info_t *dentry_fname_info_p)
{
   int len;
   /*
   ** get the size of the file name
   */
   len = strlen(name);
   if (len < ROZOFS_OBJ_NAME_MAX)
   {
     /*
     ** we can copy the filename in the inode, we don't need the encoded structure associated
     ** with the mdirent file
     */
     strncpy(inode_fname_p->name,name,len);
     inode_fname_p->name_type = ROZOFS_FNAME_TYPE_DIRECT;
     inode_fname_p->len = len;
     /*
     ** compute the hash associated with the suffix
     */
     inode_fname_p->hash_suffix = 0;
     return;
   }
   /*
   ** the file name does not fit the buffer reserved in the inode: need to store it as en encoded
   ** information that permits to get it from the mdirent file
   */
   inode_fname_p->name_type = ROZOFS_FNAME_TYPE_INDIRECT;
   memcpy(&inode_fname_p->s.name_dentry,dentry_fname_info_p,sizeof(mdirent_fid_name_info_t));
   /*
   ** store the suffix if there is enough room, otherwise set it to 0x0
   */
   inode_fname_p->s.suffix[0] = 0;
   inode_fname_p->hash_suffix = 0;
}   
     
/*
**__________________________________________________________________
*/
/**
* store the directory name in the inode
  The way the name is stored depends on the size of
  the filename: when the name is less than 62 bytes
  it is directly stored in the inode
  
  @param inode_fname_p: pointer to the array used for storing object name
  @param name: name of the object
  @param dentry_fname_info_p :pointer to the array corresponding to the fname in dentry
*/
void exp_store_dname_in_inode(rozofs_inode_fname_t *inode_fname_p,
                              char *name,
			      mdirent_fid_name_info_t *dentry_fname_info_p)
{
   int len;
   /*
   ** get the size of the file name
   */
   len = strlen(name);
   if (len < ROZOFS_OBJ_NAME_MAX)
   {
     /*
     ** we can copy the filename in the inode, we don't need the encoded structure associated
     ** with the mdirent file
     */
     strncpy(inode_fname_p->name,name,len);
     inode_fname_p->name_type = ROZOFS_FNAME_TYPE_DIRECT;
     inode_fname_p->len = len;
     inode_fname_p->hash_suffix = 0;
     return;
   }
   /*
   ** the file name does not fit the buffer reserved in the inode: need to store it as en encoded
   ** information that permits to get it from the mdirent file
   */
   inode_fname_p->name_type = ROZOFS_FNAME_TYPE_INDIRECT;
   memcpy(&inode_fname_p->s.name_dentry,dentry_fname_info_p,sizeof(mdirent_fid_name_info_t));
   inode_fname_p->s.suffix[0] = 0;
   inode_fname_p->hash_suffix = 0;
}   
          
