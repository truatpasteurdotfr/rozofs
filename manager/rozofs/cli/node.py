# -*- coding: utf-8 -*-
import sys
from rozofs.core.platform import Platform, Role
from rozofs.core.agent import ServiceStatus
import json
from rozofs.cli.output import puts
from collections import OrderedDict

ROLES_STR = {Role.EXPORTD: "exportd", Role.STORAGED: "storaged", Role.ROZOFSMOUNT: "rozofsmount"}
STR_ROLES = {"exportd": Role.EXPORTD, "storaged": Role.STORAGED, "rozofsmount": Role.ROZOFSMOUNT}

def __roles_to_strings(roles):
    strs = []
    if roles & Role.EXPORTD == Role.EXPORTD:
        strs.append("exportd")
    if roles & Role.STORAGED == Role.STORAGED:
        strs.append("storaged")
    if roles & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT:
        strs.append("rozofsmount")
    return strs


#
# utility functions
#
def __single_line():
    print >> sys.stdout, 60 * '-'


def __double_line():
    print >> sys.stdout, 60 * '='


def __args_to_roles(args):
    roles = 0
    if args.roles:
        for r in args.roles:
            roles |= STR_ROLES[r]
    else:
        roles = Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT
    return roles

def list(platform, args):
    puts(OrderedDict([(h, {'roles':__roles_to_strings(r)})
           for h, r in platform.list_nodes(__args_to_roles(args)).items()]))


# def get(platform, args):
#    nodes = platform.list_nodes()
#    if args.host[0] not in nodes.keys():
#        raise Exception('unmanaged node.')
#    # puts(nodes)
#    puts({"hostname":args.host[0], "roles":__roles_to_strings(nodes[args.host[0]])})

#
# status related functions
#
def __print_host_statuses(host, statuses):
    if statuses is not None and not statuses:
        return

    if statuses is None:
        print >> sys.stdout, ":%s :%s:%s:%s" % (host, 'down', '', '')
        return

    sr = [ROLES_STR[r] for r in statuses.keys()]
    ss = [str(s).lower() for s in statuses.values()]
    print >> sys.stdout, ":%s:%s:%s:%s" % (host, 'up', ','.join(sr), ','.join(ss))


def status(platform, args):
    statuses = platform.get_statuses(args.nodes, __args_to_roles(args))
    for h, s in statuses.items():
        if statuses is not None and not statuses:
            continue

        if statuses is None:
            puts(OrderedDict([(h, {"node":"down"})]))
        else:
            d = {"node":"up"}
            for role, status in s.items():
                d[ROLES_STR[role]] = str(status).lower()
            puts(OrderedDict([(h, d)]))
            # puts(OrderedDict([(ROLES_STR[role], str(status).lower()) for role, status in s.items()]))


    # print >> sys.stdout, ":node:node status:roles:role statuses"
    # for h, s in statuses.items():
    #    __print_host_statuses(h, s)


def start(platform, args):
    platform.start(args.nodes, __args_to_roles(args))

def stop(platform, args):
    platform.stop(args.nodes, __args_to_roles(args))


#
# configuration related functions
#
# def __exportd_config_to_string(config):
#    s = "\t\tLAYOUT: %d\n" % config.layout
#    for v in config.volumes.values():
#        s += "\t\tVOLUME: %d\n" % v.vid
#        for c in v.clusters.values():
#            s += "\t\t\tCLUSTER: %d\n" % c.cid
#            s += "\t\t\t\t%-20s %-10s\n" % ('NODE', 'SID')
#            for sid, h in c.storages.items():
#                s += "\t\t\t\t%-20s %-10d\n" % (h, sid)
#    if len(config.exports) != 0:
#        s += "\t\t%-4s %-4s %-25s %-25s %-10s %-10s\n" % ('EID', 'VID', 'ROOT', 'MD5', 'SQUOTA', 'HQUOTA')
#    for e in config.exports.values():
#        s += "\t\t%-4d %-4d %-25s %-25s %-10s %-10s\n" % (e.eid, e.vid, e.root, e.md5, e.squota, e.hquota)
#
#    return s
#
#
# def __storaged_config_to_string(config):
#    # s = "\t\tLAYOUT: %d\n" % config.layout
#    s = "\t\tPORTS: %s\n" % config.ports
#    s += "\t\t%-10s %-10s %-30s\n" % ('CID', 'SID', 'ROOT')
#    keylist = config.storages.keys()
#    keylist.sort()
#    for key in keylist:
#        st = config.storages[key]
#        s += "\t\t%-10d %-10d %-30s\n" % (st.cid, st.sid, st.root)
#    return s
#
#
# def __rozofsmount_config_to_string(config):
#    # s = "\t\tPROTOCOLS: %s\n" % config.protocols
#    s = "\t\t%-20s %-20s\n" % ('NODE', 'EXPORT')
#    for c in config:
#        s += "\t\t%-20s %-20s\n" % (c.export_host, c.export_path)
#    return s
#
#
# def __print_host_configs(host, configurations):
#    if configurations is not None and not configurations:
#        return
#
#    print >> sys.stdout, ":node:node status:roles:role statuses"
#    if configurations is None:
#        print >> sys.stdout, ":%s :%s:%s:%s" % (host, 'down', '', '')
#        return
#
#    # __double_line()
#    print >> sys.stdout, "NODE: %s - %s" % (host, 'UP')
#    for r, c in configurations.items():
#        # __single_line()
#        print >> sys.stdout, "\tROLE: %s" % ROLES_STR[r]
#        if (r & Role.EXPORTD == Role.EXPORTD):
#            print >> sys.stdout, "%s" % __exportd_config_to_string(c)
#        if (r & Role.STORAGED == Role.STORAGED):
#            print >> sys.stdout, "%s" % __storaged_config_to_string(c)
#        if (r & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT):
#            print >> sys.stdout, "%s" % __rozofsmount_config_to_string(c)
#
#
# def config(platform, args):
# #    print >> sys.stdout, "EXPORTD HOST: %s, PROTOCOL(S): %s" % (platform.get_exportd_hostname(), platform.get_sharing_protocols())
#    configurations = platform.get_configurations(args.nodes, __args_to_roles(args))
#    for h, c in configurations.items():
#        __print_host_configs(h, c)

def expand(platform, args):
    platform.add_nodes(args.hosts, args.vid)


def shrink(platform, args):
    for vid in args.vids:
        platform.remove_volume(vid)


def export(platform, args):
    platform.create_export(args.vid[0], args.name, args.passwd, args.squota, args.hquota)


def update(platform, args):
    platform.update_export(args.eid[0], args.current, args.passwd, args.squota, args.hquota)


def unexport(platform, args):
    if not args.eids:
        args.eids = None

    platform.remove_export(args.eids, args.force)


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
