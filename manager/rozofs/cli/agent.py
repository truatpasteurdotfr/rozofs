# -*- coding: utf-8 -*-

import sys
import os
import syslog

from rozofs.core.agent import AgentServer
from rozofs.core.storaged import StoragedAgent
from rozofs.core.exportd import ExportdAgent, ExportdPacemakerAgent
from rozofs.core.constants import STORAGED_MANAGER, EXPORTD_MANAGER, \
    ROZOFSMOUNT_MANAGER
from rozofs.core.rozofsmount import RozofsMountAgent
from rozofs.cli.output import puts

def status(args):
    (pid, listeners) = AgentServer().status()
    if os.getuid() is not 0:
        raise Exception("only the root user can start agent.")

    if not pid:
        raise Exception("no agent is running.")
    puts({
        'pid': pid,
        'listeners': listeners
        })

def start(args):
    if os.getuid() is not 0:
        raise Exception("only the root user can start agent.")

    (pid, listeners) = AgentServer().status()
    if pid is not None:
        raise Exception("agent is running with pid: %s." % pid)

    syslog.openlog('rozo-agent')

    managers = []
#    if PLATFORM_MANAGER in args.listeners:
#        managers.append(PlatformAgent())
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

    # print >> sys.stdout, "Starting agent, with %d listener(s) %s." % (len(args.listeners), args.listeners)
    AgentServer('/var/run/rozo-agent.pid', managers).start()


def stop(args):
    AgentServer().stop()
    # print >> sys.stdout, "Agent stopped."


def restart(args):
    stop(args)
    start(args)


def dispatch(args):
    globals()[args.action.replace('-', '_')](args)
