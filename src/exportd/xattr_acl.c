/*
 * linux/fs/ext4/acl.c
 *
 * Copyright (C) 2001-2003 Andreas Gruenbacher, <agruen@suse.de>
 */



#include <string.h>
#include <linux/fs.h>
#include <errno.h>
//#include "ext4_jbd2.h"
#include "rozofs_ext4.h"
#include "xattr.h"
#include "acl.h"



/*
 * Extended attribute handlers
 */
static size_t
ext4_xattr_list_acl_access(struct dentry *dentry, char *list, size_t list_len,
			   const char *name, size_t name_len, int type)
{
	const size_t size = sizeof(POSIX_ACL_XATTR_ACCESS);

//	if (!test_opt(dentry->d_sb, POSIX_ACL))
//		return 0;
	if (list && size <= list_len)
		memcpy(list, POSIX_ACL_XATTR_ACCESS, size);
	return size;
}

static size_t
ext4_xattr_list_acl_default(struct dentry *dentry, char *list, size_t list_len,
			    const char *name, size_t name_len, int type)
{
	const size_t size = sizeof(POSIX_ACL_XATTR_DEFAULT);

//	if (!test_opt(dentry->d_sb, POSIX_ACL))
//		return 0;
	if (list && size <= list_len)
		memcpy(list, POSIX_ACL_XATTR_DEFAULT, size);
	return size;
}

static int
ext4_xattr_get_acl(struct dentry *dentry, const char *name, void *buffer,
		   size_t size, int type)
{

	if (strcmp(name, "") != 0)
		return -EINVAL;
//	if (!test_opt(dentry->d_sb, POSIX_ACL))
//		return -EOPNOTSUPP;

	return ext4_xattr_get(dentry->d_inode, EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT,
			      name, buffer, size);

}

static int
ext4_xattr_set_acl(struct dentry *dentry, const char *name, const void *value,
		   size_t size, int flags, int type)
{

	if (strcmp(name, "") != 0)
		return -EINVAL;
		
	return ext4_xattr_set(dentry->d_inode, EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT,
		      name, value, size, flags);

}

const struct xattr_handler ext4_xattr_acl_access_handler = {
	.prefix	= POSIX_ACL_XATTR_ACCESS,
//	.flags	= ACL_TYPE_ACCESS,
	.list	= ext4_xattr_list_acl_access,
	.get	= ext4_xattr_get_acl,
	.set	= ext4_xattr_set_acl,
};

const struct xattr_handler ext4_xattr_acl_default_handler = {
	.prefix	= POSIX_ACL_XATTR_DEFAULT,
//	.flags	= ACL_TYPE_DEFAULT,
	.list	= ext4_xattr_list_acl_default,
	.get	= ext4_xattr_get_acl,
	.set	= ext4_xattr_set_acl,
};
