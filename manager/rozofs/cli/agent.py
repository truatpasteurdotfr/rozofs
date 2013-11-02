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
import os
import syslog

from rozofs.core.agent import AgentServer
from rozofs.core.storaged import StoragedAgent
from rozofs.core.exportd import ExportdAgent, ExportdPacemakerAgent
from rozofs.core.constants import STORAGED_MANAGER, EXPORTD_MANAGER, \
    ROZOFSMOUNT_MANAGER
from rozofs.core.rozofsmount import RozofsMountAgent
from rozofs.cli.output import ordered_puts
from collections import OrderedDict

def status(args):
    (pid, listeners) = AgentServer().status()

    if not pid:
        raise Exception("no agent is running.")
    ordered_puts(OrderedDict([("pid", int(pid)), ("listeners", listeners)]))

def start(args):
    if os.getuid() is not 0:
        raise Exception("only the root user can start agent.")

    (pid, listeners) = AgentServer().status()
    if pid is not None:
        raise Exception("agent is running with pid: %s." % pid)

    syslog.openlog('rozo-agent')

    if not args.listeners:
        args.listeners = [EXPORTD_MANAGER, STORAGED_MANAGER, ROZOFSMOUNT_MANAGER]

    managers = []
    if STORAGED_MANAGER in args.listeners:
        managers.append(StoragedAgent())
    if EXPORTD_MANAGER in args.listeners:
        if args.pacemaker:
            managers.append(ExportdPacemakerAgent())
        else:
            managers.append(ExportdAgent())
    if ROZOFSMOUNT_MANAGER in args.listeners:
        managers.append(RozofsMountAgent())

    if len(managers) is 0:
        raise "no suitable manager."

    AgentServer('/var/run/rozo-agent.pid', managers).start()


def stop(args):
    if os.getuid() is not 0:
        raise Exception("only the root user can stop agent.")
    AgentServer().stop()

def restart(args):
    if os.getuid() is not 0:
        raise Exception("only the root user can restart agent.")
    stop(args)
    start(args)

def dispatch(args):
    globals()[args.action.replace('-', '_')](args)
