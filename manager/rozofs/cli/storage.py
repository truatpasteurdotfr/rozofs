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
from rozofs.core.storaged import ListenConfig
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

    for host in args.hosts:
        if not host in platform.get_configurations([args.exportd],Role.STORAGED):
                raise Exception('%s: invalid storaged server.' % host)
        configuration = platform.get_configurations([args.exportd],
                Role.STORAGED)[host][Role.STORAGED]
        sid_l={}
        sid_l[host]=[]
        lid_l={}
        for lconfig in configuration.listens:
            lid_l = OrderedDict([
                ('addr', lconfig.addr),
                ('port', lconfig.port)
            ])
            sid_l[host].append(lid_l)

        ordered_puts(sid_l)

def add(platform, args):
    
    for host in args.hosts:
        if not host in platform.get_configurations([args.exportd],Role.STORAGED):
                raise Exception('%s: invalid storaged server.' % host)
        
        configurations = platform._get_nodes(host)[host].get_configurations()
        configuration = configurations[Role.STORAGED]
        for listener in configuration.listens:
            # if given interface is '*', remove existing interfaces
            if args.interface == "*":
                configuration.listens = []
                continue
            elif args.interface == listener.addr:
                if args.port == listener.port:
                    raise Exception('entry %s:%s already exists.' %
                            (args.interface, args.port))
            if listener.addr == '*':
                configuration.listens = []
        sid_l={}
        sid_l[host]=[]
        lid_l={}
        lconfig = ListenConfig(args.interface, args.port)
        print lconfig
        configuration.listens.append(lconfig)
        configurations[Role.STORAGED] = configuration
        platform._get_nodes(host)[host].set_configurations(configurations)

        for lconfig in configuration.listens:
            lid_l = OrderedDict([
                ('addr', lconfig.addr),
                ('port', lconfig.port)
            ])
            sid_l[host].append(lid_l)

        ordered_puts(sid_l)

def remove(platform, args):
    
    for host in args.hosts:
        if not host in platform.get_configurations([args.exportd],Role.STORAGED):
                raise Exception('%s: invalid storaged server.' % host)
        
        configurations = platform._get_nodes(host)[host].get_configurations()
        configuration = configurations[Role.STORAGED]
        check = True
        for listener in configuration.listens:
            if args.interface == listener.addr:
                if args.port == listener.port:
                    configuration.listens.remove(listener)
                    check = False
        if check:
            raise Exception('entry %s:%s does not exist.' % (args.interface,
            args.port))
        sid_l={}
        sid_l[host]=[]
        lid_l={}
        configurations[Role.STORAGED] = configuration

        platform._get_nodes(host)[host].set_configurations(configurations)

        for lconfig in configuration.listens:
            lid_l = OrderedDict([
                ('addr', lconfig.addr),
                ('port', lconfig.port)
            ])
            sid_l[host].append(lid_l)

        ordered_puts(sid_l)


def dispatch(args):
    p = Platform(args.exportd)
    globals()[args.action.replace('-', '_')](p, args)
