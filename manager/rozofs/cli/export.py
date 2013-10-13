# -*- coding: utf-8 -*-
import sys
from rozofs.core.platform import Platform, Role
from rozofs.core.agent import ServiceStatus
from rozofs.cli.output import puts


def list(platform, args):
    configurations = platform.get_configurations([args.exportd], Role.EXPORTD)
    if configurations[args.exportd] is None:
        raise Exception("exportd node is off line.")

    configuration = configurations[args.exportd][Role.EXPORTD]
    
    for eid, econfig in configuration.exports.items():
        puts([{'eid '+str(eid):
            [{  'vid': econfig.vid,
                'root': econfig.root,
                'md5': econfig.md5,
                'squota' : econfig.squota,
                'hquota' : econfig.hquota}
    ]}])

def create(platform, args):
    platform.create_export(args.vid[0], args.name, args.passwd, args.squota, args.hquota)


def update(platform, args):
    platform.update_export(args.eid[0], args.current, args.passwd, args.squota, args.hquota)


def remove(platform, args):
    if not args.eids:
        args.eids = None

    platform.remove_export(args.eids, args.force)

def stat(platform, args):
    configurations = platform.get_configurations([args.exportd], Role.EXPORTD)
    if configurations[args.exportd] is None:
        raise Exception("exportd node is off line.")

    configuration = configurations[args.exportd][Role.EXPORTD]
    
    for eid, estat in configuration.stats.estats.items():
        puts([{'eid '+str(eid):
            [{  'vid': estat.vid,
                'bsize': estat.bsize,
                'blocks': estat.blocks,
                'bfree' : estat.bfree,
                'files' : estat.files,
                'ffree' : estat.ffree}
    ]}])

def mount(platform, args):
    if not args.eids:
        args.eids = None

    platform.mount_export(args.eids, args.nodes)


def umount(platform, args):
    if not args.eids:
        args.eids = None

    platform.umount_export(args.eids, args.nodes)


def dispatch(args):
    p = Platform(args.exportd)
    globals()[args.action.replace('-', '_')](p, args)
