/*
  File: fs/ext4/acl.h

  (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/
#ifndef ACL_H
#define ACL_H
#include "posix_acl_attr.h"

#define EXT4_ACL_VERSION	0x0001

typedef struct {
	__le16		e_tag;
	__le16		e_perm;
	__le32		e_id;
} ext4_acl_entry;

typedef struct {
	__le16		e_tag;
	__le16		e_perm;
} ext4_acl_entry_short;

typedef struct {
	__le32		a_version;
} ext4_acl_header;

static inline size_t ext4_acl_size(int count)
{
	if (count <= 4) {
		return sizeof(ext4_acl_header) +
		       count * sizeof(ext4_acl_entry_short);
	} else {
		return sizeof(ext4_acl_header) +
		       4 * sizeof(ext4_acl_entry_short) +
		       (count - 4) * sizeof(ext4_acl_entry);
	}
}

static inline int ext4_acl_count(size_t size)
{
	ssize_t s;
	size -= sizeof(ext4_acl_header);
	s = size - 4 * sizeof(ext4_acl_entry_short);
	if (s < 0) {
		if (size % sizeof(ext4_acl_entry_short))
			return -1;
		return size / sizeof(ext4_acl_entry_short);
	} else {
		if (s % sizeof(ext4_acl_entry))
			return -1;
		return s / sizeof(ext4_acl_entry) + 4;
	}
}




#endif 

