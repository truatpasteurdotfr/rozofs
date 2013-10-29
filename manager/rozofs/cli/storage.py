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
    configurations = platform.get_configurations([args.exportd], Role.STORAGED)

    sid_l={}
    for stor in configurations:
        sid_l[stor] = []
        lid_l={}
        for lconfig in configurations[stor][Role.STORAGED].listens:
            lid_l = OrderedDict([
                ('addr', lconfig.addr),
                ('port', lconfig.port)
            ])
            sid_l[stor].append(lid_l)

    ordered_puts(sid_l)

def get(platform, args):
    configurations = platform.get_configurations([args.exportd], Role.EXPORTD)
    if configurations[args.exportd] is None:
        raise Exception("exportd node is off line.")

    configuration = configurations[args.exportd][Role.EXPORTD]
    vconfig = configuration.volumes[args.vid[0]]
    vstat = configuration.stats.vstats[args.vid[0]]

    cid_l = {}
    for vid, vstat in configuration.stats.vstats.items():
        for cid, cstat in vstat.cstats.items():
            for sid, sstat in cstat.sstats.items():
                sid_l['cid/sid ' + str(cid) + '/' + str(sid)] = OrderedDict([
                ('host', sstat.host),
                ('size', sstat.size),
                ('free', sstat.free)
            ])

def add(platform, args):
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
