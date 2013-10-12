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
    
    for vid, vconfig in configuration.volumes.items():
        vstat = configuration.stats.vstats[vid]
        puts([{'vid '+str(vid):
            [{  'bsize': vstat.bsize,
                'bfree': vstat.bfree,
                'blocks': vstat.blocks},
             {  'cid '+str(cid):
                [{  'size' : cstat.size,
                    'free' : cstat.free},
                    {   'sid '+str(sid):
                        {   'host': sstat.host,
                            'size': sstat.size,
                            'free': sstat.free}
                            for sid, sstat in cstat.sstats.items()
                    }
                ] for cid, cstat in vstat.cstats.items()
             }
            ]},
        ])

def get(platform, args):
    configurations = platform.get_configurations([args.exportd], Role.EXPORTD)
    if configurations[args.exportd] is None:
        raise Exception("exportd node is off line.")

    configuration = configurations[args.exportd][Role.EXPORTD]
    vconfig = configuration.volumes[args.vid[0]]
    vstat = configuration.stats.vstats[args.vid[0]]

    for cid, cconfig in vstat.cstats.items():
        puts([{'cid '+str(cid):
            [{'bsize' : vstat.bsize,
            'bfree' : vstat.bfree,
            'blocks' : vstat.blocks},
            {'sid '+str(sid):
                {'host': sconfig.host,
                'size': sconfig.size,
                'free': sconfig.free}
                    for sid, sconfig in cconfig.sstats.items()
            }]},
        ])

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
    if args.vid:
        platform.add_nodes(args.hosts, args.vid[0])
    else:
        platform.add_nodes(args.hosts, args.vid)


def remove(platform, args):
    for vid in args.vids:
        platform.remove_volume(vid)


def dispatch(args):
    p = Platform(args.exportd)
    globals()[args.action.replace('-', '_')](p, args)
