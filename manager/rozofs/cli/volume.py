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
from rozofs.cli.output import puts
from rozofs.cli.output import ordered_puts
from collections import OrderedDict

def list(platform, args):
    configurations = platform.get_configurations([args.exportd], Role.EXPORTD)
    if configurations[args.exportd] is None:
        raise Exception("exportd node is off line.")

    configuration = configurations[args.exportd][Role.EXPORTD]
    print configuration.volumes

    export_l = {}
    list_l = []
    for vid, volume in configuration.volumes.items():
        volume_l = []
        for cid, cluster in volume.clusters.items():
            cluster_l = []
            for sid, storage in cluster.storages.items():
                cluster_l.append({'STORAGE ' + str(sid): storage})
            volume_l.append({'CLUSTER ' + str(cid): cluster_l})
        list_l.append({'VOLUME ' + str(vid): volume_l})
    export_l.update({'EXPORTD on ' + str(args.exportd): list_l})
    ordered_puts(export_l)

def stat(platform, args):
    configurations = platform.get_configurations([args.exportd], Role.EXPORTD)
    if configurations[args.exportd] is None:
        raise Exception("exportd node is off line.")

    configuration = configurations[args.exportd][Role.EXPORTD]
    
    export_l = {}
    stat_l = []
    for vid, vstat in configuration.stats.vstats.items():
        volume_l = []
        volume_l.append({'bsize': vstat.bsize})
        volume_l.append({'bfree': vstat.bfree})
        volume_l.append({'blocks': vstat.blocks})
        for cid, cstat in vstat.cstats.items():
            cluster_l = []
            cluster_l.append({'size': cstat.size})
            cluster_l.append({'free': cstat.free})
            for sid, sstat in cstat.sstats.items():
                storage_l = []
                storage_l.append({'host': sstat.host})
                storage_l.append({'size': sstat.size})
                storage_l.append({'free': sstat.free})
                cluster_l.append({'STORAGE ' + str(sid): storage_l})
            volume_l.append({'CLUSTER ' + str(cid): cluster_l})
        stat_l.append({'VOLUME ' + str(vid): volume_l})
    export_l.update({'EXPORTD on ' + str(args.exportd): stat_l})
    ordered_puts(export_l)

def get(platform, args):
    configurations = platform.get_configurations([args.exportd], Role.EXPORTD)
    if configurations[args.exportd] is None:
        raise Exception("exportd node is off line.")

    configuration = configurations[args.exportd][Role.EXPORTD]
    vconfig = configuration.volumes[args.vid[0]]
    vstat = configuration.stats.vstats[args.vid[0]]

    get_l = {}
    volume_l = []
    for cid, cstat in vstat.cstats.items():
        cluster_l = []
        cluster_l.append({'size': cstat.size})
        cluster_l.append({'free': cstat.free})
        for sid, sstat in cstat.sstats.items():
            storage_l = []
            storage_l.append({'host': sstat.host})
            storage_l.append({'size': sstat.size})
            storage_l.append({'free': sstat.free})
            cluster_l.append({'STORAGE ' + str(sid): storage_l})
        volume_l.append({'CLUSTER ' + str(cid): cluster_l})
    get_l.update({'VOLUME ' + str(args.vid[0]): volume_l})

    ordered_puts(get_l)

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
