# -*- coding: utf-8 -*-
import sys
from rozofs.core.platform import Platform, Role
from rozofs.core.agent import ServiceStatus


def list(platform, args):
    nodes = platform.list_nodes(__args_to_roles(args))
    print >> sys.stdout, ":%s:%s" % ("node", "roles")
    for h, r in nodes.items():
        print >> sys.stdout, ":%s:%s" % (h, ','.join(__roles_to_strings(r)))



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


def dispatch(args):
    p = Platform(args.exportd)
    globals()[args.action.replace('-', '_')](p, args)
