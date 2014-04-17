/*
  File: fs/ext4/xattr.h

  On-disk format of extended attributes for the ext4 filesystem.

  (C) 2001 Andreas Gruenbacher, <a.gruenbacher@computer.org>
*/
#ifndef XATTR_H
#define XATTR_H
//#include <linux/xattr.h>
#include <rozofs/rozofs.h>
#include <rozofs/common/mattr.h>
#include "exp_cache.h"



#define XATTR_CREATE	0x1	/* set value, fail if attr already exists */
#define XATTR_REPLACE	0x2	/* set value, fail if attr does not exist */

/* Namespaces */
#define XATTR_OS2_PREFIX "os2."
#define XATTR_OS2_PREFIX_LEN (sizeof (XATTR_OS2_PREFIX) - 1)

#define XATTR_SECURITY_PREFIX	"security."
#define XATTR_SECURITY_PREFIX_LEN (sizeof (XATTR_SECURITY_PREFIX) - 1)

#define XATTR_SYSTEM_PREFIX "system."
#define XATTR_SYSTEM_PREFIX_LEN (sizeof (XATTR_SYSTEM_PREFIX) - 1)

#define XATTR_TRUSTED_PREFIX "trusted."
#define XATTR_TRUSTED_PREFIX_LEN (sizeof (XATTR_TRUSTED_PREFIX) - 1)

#define XATTR_USER_PREFIX "user."
#define XATTR_USER_PREFIX_LEN (sizeof (XATTR_USER_PREFIX) - 1)

/* Security namespace */
#define XATTR_EVM_SUFFIX "evm"
#define XATTR_NAME_EVM XATTR_SECURITY_PREFIX XATTR_EVM_SUFFIX

#define XATTR_SELINUX_SUFFIX "selinux"
#define XATTR_NAME_SELINUX XATTR_SECURITY_PREFIX XATTR_SELINUX_SUFFIX

#define XATTR_SMACK_SUFFIX "SMACK64"
#define XATTR_SMACK_IPIN "SMACK64IPIN"
#define XATTR_SMACK_IPOUT "SMACK64IPOUT"
#define XATTR_SMACK_EXEC "SMACK64EXEC"
#define XATTR_SMACK_TRANSMUTE "SMACK64TRANSMUTE"
#define XATTR_SMACK_MMAP "SMACK64MMAP"
#define XATTR_NAME_SMACK XATTR_SECURITY_PREFIX XATTR_SMACK_SUFFIX
#define XATTR_NAME_SMACKIPIN	XATTR_SECURITY_PREFIX XATTR_SMACK_IPIN
#define XATTR_NAME_SMACKIPOUT	XATTR_SECURITY_PREFIX XATTR_SMACK_IPOUT
#define XATTR_NAME_SMACKEXEC	XATTR_SECURITY_PREFIX XATTR_SMACK_EXEC
#define XATTR_NAME_SMACKTRANSMUTE XATTR_SECURITY_PREFIX XATTR_SMACK_TRANSMUTE
#define XATTR_NAME_SMACKMMAP XATTR_SECURITY_PREFIX XATTR_SMACK_MMAP

#define XATTR_CAPS_SUFFIX "capability"
#define XATTR_NAME_CAPS XATTR_SECURITY_PREFIX XATTR_CAPS_SUFFIX

#define XATTR_POSIX_ACL_ACCESS  "posix_acl_access"
#define XATTR_NAME_POSIX_ACL_ACCESS XATTR_SYSTEM_PREFIX XATTR_POSIX_ACL_ACCESS
#define XATTR_POSIX_ACL_DEFAULT  "posix_acl_default"
#define XATTR_NAME_POSIX_ACL_DEFAULT XATTR_SYSTEM_PREFIX XATTR_POSIX_ACL_DEFAULT


//lv2_entry_t;
struct dentry;

struct xattr_handler {
	const char *prefix;
	int flags;	/* fs private flags passed back to the handlers */
	size_t (*list)(struct dentry *dentry, char *list, size_t list_size,
		       const char *name, size_t name_len, int handler_flags);
	int (*get)(struct dentry *dentry, const char *name, void *buffer,
		   size_t size, int handler_flags);
	int (*set)(struct dentry *dentry, const char *name, const void *buffer,
		   size_t size, int flags, int handler_flags);
};

struct xattr {
	char *name;
	void *value;
	size_t value_len;
};

extern const struct xattr_handler *ext4_xattr_handler_map[];


/* Magic value in attribute blocks */
#define EXT4_XATTR_MAGIC		0xEA020000

/* Maximum number of references to one attribute block */
#define EXT4_XATTR_REFCOUNT_MAX		1024

/* Name indexes */
#define EXT4_XATTR_INDEX_USER			1
#define EXT4_XATTR_INDEX_POSIX_ACL_ACCESS	2
#define EXT4_XATTR_INDEX_POSIX_ACL_DEFAULT	3
#define EXT4_XATTR_INDEX_TRUSTED		4
#define	EXT4_XATTR_INDEX_LUSTRE			5
#define EXT4_XATTR_INDEX_SECURITY	        6
#define EXT4_XATTR_INDEX_SYSTEM  	        7

struct ext4_xattr_header {
//        uint64_t owner_inode;   /**< inode of the owner of the extended attributes  */
	__le32	h_magic;	/* magic number for identification */
	__le32	h_refcount;	/* reference count */
	__le32	h_blocks;	/* number of disk blocks used */
	__le32	h_hash;		/* hash value of all attributes */
	__u32	h_reserved[4];	/* zero right now */
};

struct ext4_xattr_ibody_header {
//        uint64_t owner_inode;   /**< inode of the owner of the extended attributes  */
	__le32	h_magic;	/* magic number for identification */
};

struct ext4_xattr_entry {
	__u8	e_name_len;	/* length of name */
	__u8	e_name_index;	/* attribute name index */
	__le16	e_value_offs;	/* offset in disk block of value */
	__le32	e_value_block;	/* disk block attribute is stored on (n/i) */
	__le32	e_value_size;	/* size of attribute value */
	__le32	e_hash;		/* hash value of name and value */
	char	e_name[0];	/* attribute name */
};

#define EXT4_XATTR_PAD_BITS		2
#define EXT4_XATTR_PAD		(1<<EXT4_XATTR_PAD_BITS)
#define EXT4_XATTR_ROUND		(EXT4_XATTR_PAD-1)
#define EXT4_XATTR_LEN(name_len) \
	(((name_len) + EXT4_XATTR_ROUND + \
	sizeof(struct ext4_xattr_entry)) & ~EXT4_XATTR_ROUND)
#define EXT4_XATTR_NEXT(entry) \
	((struct ext4_xattr_entry *)( \
	 (char *)(entry) + EXT4_XATTR_LEN((entry)->e_name_len)))
#define EXT4_XATTR_SIZE(size) \
	(((size) + EXT4_XATTR_ROUND) & ~EXT4_XATTR_ROUND)

#define IHDR(inode, raw_inode) \
	((struct ext4_xattr_ibody_header *) \
		((void *)raw_inode + \
		inode->attributes.s.i_extra_isize))
#define IFIRST(hdr) ((struct ext4_xattr_entry *)((hdr)+1))



extern const struct xattr_handler ext4_xattr_user_handler;
extern const struct xattr_handler ext4_xattr_trusted_handler;
extern const struct xattr_handler ext4_xattr_acl_access_handler;
extern const struct xattr_handler ext4_xattr_acl_default_handler;
extern const struct xattr_handler ext4_xattr_security_handler;
extern const struct xattr_handler ext4_xattr_system_handler;

extern ssize_t ext4_listxattr(struct dentry *, char *, size_t);

extern int ext4_xattr_get(lv2_entry_t *, int, const char *, void *, size_t);
extern int ext4_xattr_set(lv2_entry_t *, int, const char *, const void *, size_t, int);
extern int ext4_xattr_set_handle(void *, lv2_entry_t *, int, const char *, const void *, size_t, int);

extern void ext4_xattr_delete_inode(void *, lv2_entry_t *);

extern int ext4_expand_extra_isize_ea(lv2_entry_t *inode, int new_extra_isize,
			    struct ext4_inode *raw_inode, void *handle);

extern void ext4_exit_xattr(void);

extern const struct xattr_handler *ext4_xattr_handlers[];



extern int ext4_init_security(void *handle, lv2_entry_t *inode,
			      lv2_entry_t *dir, const struct qstr *qstr);

#endif
