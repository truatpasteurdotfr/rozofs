/*
 * linux/fs/ext4/xattr_system.c
 * Handler for extended user attributes.
 *
 * Copyright (C) 2001 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
 */

#include <string.h>
#include <linux/fs.h>
#include <errno.h>
//#include "ext4_jbd2.h"
#include "rozofs_ext4.h"
#include "xattr.h"

static size_t
ext4_xattr_system_list(struct dentry *dentry, char *list, size_t list_size,
		     const char *name, size_t name_len, int type)
{
	const size_t prefix_len = XATTR_SYSTEM_PREFIX_LEN;
	const size_t total_len = prefix_len + name_len + 1;


	if (list && total_len <= list_size) {
		memcpy(list, XATTR_SYSTEM_PREFIX, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return total_len;
}

static int
ext4_xattr_system_get(struct dentry *dentry, const char *name,
		    void *buffer, size_t size, int type)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return ext4_xattr_get(dentry->d_inode, EXT4_XATTR_INDEX_SYSTEM,
			      name, buffer, size);
}

static int
ext4_xattr_system_set(struct dentry *dentry, const char *name,
		    const void *value, size_t size, int flags, int type)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	return ext4_xattr_set(dentry->d_inode, EXT4_XATTR_INDEX_SYSTEM,
			      name, value, size, flags);
}

const struct xattr_handler ext4_xattr_system_handler = {
	.prefix	= XATTR_SYSTEM_PREFIX,
	.list	= ext4_xattr_system_list,
	.get	= ext4_xattr_system_get,
	.set	= ext4_xattr_system_set,
};
