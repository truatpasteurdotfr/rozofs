# -*- coding: utf-8 -*-

AGENT_PORT = 9999

PLATFORM_MANAGER = "platform"
EXPORTD_MANAGER = "exportd"
STORAGED_MANAGER = "storaged"
SHARE_MANAGER = "share"

LAYOUT_NONE = -1
LAYOUT_2_3_4 = 0
LAYOUT_4_6_8 = 1
LAYOUT_8_12_16 = 2

LAYOUT_INVERSE = 0
LAYOUT_FORWARD = 1
LAYOUT_SAFE = 2
LAYOUT_VALUES = [[2, 3, 4], [4, 6, 8], [8, 12, 16]]

EXPORTD_HOSTNAME = "exportd_hostname"
EXPORTD_STANDALONE = "exportd_standalone"
PROTOCOLS = "protocols"
# PROTOCOLS_VALUES = ["nfs", "cifs", "afp"]
# only NFS for now
PROTOCOLS_VALUES = ["nfs"]

LAYOUT = "layout"
PORTS = "ports"
STORAGES = "storages"
STORAGE_SID = "sid"
STORAGE_ROOT = "root"

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

