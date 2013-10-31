# -*- coding: utf-8 -*-
#
# Copyright (c) 2013 Fizians SAS. <http://www.fizians.com>
# This file is part of Rozofs.
#
# Rozofs is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published
# by the Free Software Foundation, version 2.
#
# Rozofs is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see
# <http://www.gnu.org/licenses/>.

import sys
from rozofs.core.platform import Platform, Role
from rozofs.core.agent import ServiceStatus
from rozofs.core.constants import STORAGED_MANAGER, EXPORTD_MANAGER, \
ROZOFSMOUNT_MANAGER
import json
from rozofs.cli.output import ordered_puts
from collections import OrderedDict

ROLES_STR = {Role.EXPORTD: "EXPORTD", Role.STORAGED: "STORAGED", Role.ROZOFSMOUNT: "ROZOFSMOUNT"}
STR_ROLES = {"exportd": Role.EXPORTD, "storaged": Role.STORAGED, "rozofsmount": Role.ROZOFSMOUNT}

def __roles_to_strings(roles):
    strs = []
    if roles & Role.EXPORTD == Role.EXPORTD:
        strs.append("EXPORTD")
    if roles & Role.STORAGED == Role.STORAGED:
        strs.append("STORAGED")
    if roles & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT:
        strs.append("ROZOFSMOUNT")
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
    ordered_puts(OrderedDict([(h, {'roles':__roles_to_strings(r)})
           for h, r in platform.list_nodes(__args_to_roles(args)).items()]))


# def get(platform, args):
#    nodes = platform.list_nodes()
#    if args.host[0] not in nodes.keys():
#        raise Exception('unmanaged node.')
#    # ordered_puts(nodes)
#    ordered_puts({"hostname":args.host[0], "roles":__roles_to_strings(nodes[args.host[0]])})

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
            ordered_puts(OrderedDict([(h, {"node":"down"})]))
        else:
            d = {"node":"up"}
            for role, status in s.items():
                d[ROLES_STR[role]] = str(status).lower()
            ordered_puts(OrderedDict([(h, d)]))
            # ordered_puts(OrderedDict([(ROLES_STR[role], str(status).lower()) for role, status in s.items()]))


    # print >> sys.stdout, ":node:node status:roles:role statuses"
    # for h, s in statuses.items():
    #    __print_host_statuses(h, s)


def start(platform, args):
    platform.start(args.nodes, __args_to_roles(args))

def stop(platform, args):
    platform.stop(args.nodes, __args_to_roles(args))

def __exportd_config_to_string(config):
    s = "\t\tLAYOUT: %d\n" % config.layout
    for v in config.volumes.values():
        s += "\t\tVOLUME: %d\n" % v.vid
        for c in v.clusters.values():
            s += "\t\t\tCLUSTER: %d\n" % c.cid
            s += "\t\t\t\t%-20s %-10s\n" % ('NODE', 'SID')
            for sid, h in c.storages.items():
                s += "\t\t\t\t%-20s %-10d\n" % (h, sid)
    if len(config.exports) != 0:
        s += "\t\t%-4s %-4s %-25s %-25s %-10s %-10s\n" % ('EID', 'VID', 'ROOT', 'MD5', 'SQUOTA', 'HQUOTA')
    for e in config.exports.values():
        s += "\t\t%-4d %-4d %-25s %-25s %-10s %-10s\n" % (e.eid, e.vid, e.root, e.md5, e.squota, e.hquota)

    return s


def __storaged_config_to_string(config):
    s = "\t\t%-21s %-10s\n" % ('INTERFACE', 'PORT')
    for lconfig in config.listens:
        s += "\t\t%-21s %-10s\n" % (lconfig.addr, lconfig.port)
    s += "\t\t%-10s %-10s %-30s\n" % ('CID', 'SID', 'ROOT')
    keylist = config.storages.keys()
    keylist.sort()
    for key in keylist:
        st = config.storages[key]
        s += "\t\t%-10d %-10d %-30s\n" % (st.cid, st.sid, st.root)
    return s


def __rozofsmount_config_to_string(config):
    s = "\t\t%-20s %-20s\n" % ('NODE', 'EXPORT')
    for c in config:
        s += "\t\t%-20s %-20s\n" % (c.export_host, c.export_path)
    return s


def __print_host_configs(host, configurations):
    if configurations is not None and not configurations:
        return

    print >> sys.stdout, ":node:node status:roles:role statuses"
    if configurations is None:
        print >> sys.stdout, ":%s :%s:%s:%s" % (host, 'down', '', '')
        return

    # __double_line()
    print >> sys.stdout, "NODE: %s - %s" % (host, 'UP')
    for r, c in configurations.items():
        # __single_line()
        print >> sys.stdout, "\tROLE: %s" % ROLES_STR[r]
        if (r & Role.EXPORTD == Role.EXPORTD):
            print >> sys.stdout, "%s" % __exportd_config_to_string(c)
        if (r & Role.STORAGED == Role.STORAGED):
            print >> sys.stdout, "%s" % __storaged_config_to_string(c)
        if (r & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT):
            print >> sys.stdout, "%s" % __rozofsmount_config_to_string(c)


def config(platform, args):
    if not args.roles:
        args.roles = [EXPORTD_MANAGER, STORAGED_MANAGER, ROZOFSMOUNT_MANAGER]
    configurations = platform.get_configurations(args.nodes, __args_to_roles(args))
    host_l = {}
    for h, c in configurations.items():
        if c is not None and not c:
            return

        if c is None:
            raise Exception ('%s is down.' % h)

        role_l=[]
        for role, config in c.items():
            if (role & Role.EXPORTD == Role.EXPORTD):
                exportd_l = []
                volume_l = []
                for v in config.volumes.values():
                    cluster_l = []
                    for cluster in v.clusters.values():
                        s_l = []
                        for s, hhh in cluster.storages.items():
                            s_l.append({'sid ' + str(s): hhh})
                    cluster_l.append({'cluster ' + str(cluster.cid): s_l})
                    volume_l.append({'volume ' + str(v.vid): cluster_l})
                exportd_l.append({'VOLUME':volume_l})
                if len(config.exports) != 0:
                    for e in config.exports.values():
                        export_l = OrderedDict([
                            ('vid', e.vid),
                            ('root', e.root),
                            ('md5', e.md5),
                            ('squota', e.squota),
                            ('hquota', e.hquota)])
                        exportd_l.append({'EXPORT':export_l})
                role_l.append({'EXPORTD':exportd_l})
                            
            if (role & Role.STORAGED == Role.STORAGED):
                storage_l = []
                interface_l = []
                for lconfig in config.listens:
                    interface_l.append({lconfig.addr : lconfig.port})
                storage_l.append({'INTERFACE':interface_l})
                keylist = config.storages.keys()
                keylist.sort()
                st_l = []
                for key in keylist:
                    st = config.storages[key]
                    st_l.append({'cid ' + str(st.cid) + ', sid '+
                        str(st.sid) : st.root})
                storage_l.append({'STORAGE': st_l})
                role_l.append({'STORAGED': storage_l})
                    
            if (role & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT):
                exp_l = []
                for c in config:
                    exp_l.append({'node ' +
                        str(c.export_host) : c.export_path})
                role_l.append({'ROZOFSMOUNT': exp_l})
            
            host_l.update({'NODE: ' + str(h) : role_l})

    ordered_puts(host_l)
    #__print_host_configs(h, c)
        

def dispatch(args):
    p = Platform(args.exportd)
    globals()[args.action.replace('-', '_')](p, args)
