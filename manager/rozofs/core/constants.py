# -*- coding: utf-8 -*-

AGENT_PORT = 9999

PLATFORM_MANAGER = "platform"
EXPORTD_MANAGER = "exportd"
STORAGED_MANAGER = "storaged"
ROZOFSMOUNT_MANAGER = "rozofsmount"
NFS_MANAGER = "nfs"
# SHARE_MANAGER = "share"

LAYOUT_NONE = -1
LAYOUT_2_3_4 = 0
LAYOUT_4_6_8 = 1
LAYOUT_8_12_16 = 2

LAYOUT_INVERSE = 0
LAYOUT_FORWARD = 1
LAYOUT_SAFE = 2
LAYOUT_VALUES = [[2, 3, 4], [4, 6, 8], [8, 12, 16]]

EXPORTD_HOSTNAME = "exportd_hostname"
# PROTOCOLS = "protocols"
# PROTOCOLS_VALUES = ["nfs", "cifs", "afp"]
# only NFS for now
# PROTOCOLS_VALUES = ["nfs"]

LAYOUT = "layout"
LISTEN = "listen"
LISTEN_ADDR = "addr"
LISTEN_PORT = "port"
STORAGES = "storages"
STORAGE_CID = "cid"
STORAGE_SID = "sid"
STORAGE_ROOT = "root"
SID_MAX = 255

VOLUMES = "volumes"
VOLUME_VID = "vid"
VOLUME = "volume"
VOLUME_CIDS = "cids"
VOLUME_CID = "cid"
VOLUME_SIDS = "sids"
VOLUME_SID = "sid"
VOLUME_HOST = "host"
EXPORTS = "exports"
EXPORT_EID = "eid"
EXPORT_ROOT = "root"
EXPORT_MD5 = "md5"
EXPORT_HQUOTA = "hquota"
EXPORT_SQUOTA = "squota"

STORAGES_ROOT = "/srv/rozofs/storages"
EXPORTS_ROOT = "/srv/rozofs/exports"
