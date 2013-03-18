# -*- coding: utf-8 -*-

import argparse
import socket
from rozofs.cli.agent import agent_dispatch
from rozofs.core.constants import EXPORTD_MANAGER, STORAGED_MANAGER, \
    ROZOFSMOUNT_MANAGER
from rozofs.cli.platform import platform_dispatch, STR_ROLES
from rozofs import __version__

__main_parser = argparse.ArgumentParser(description='Rozofs storage platform management tool.', usage='%(prog)s [options] [command] [options]')
__main_parser.add_argument('-V', '--version', action='version', version=__version__, help='get version.')
__main_parser.add_argument('-d', '--debug', action='store_true', help='set debugging on')
__cmd_parser = __main_parser.add_subparsers(help='commands list')

#
# commands
#
def __add_command_parser(command, helpmsg, dispatch, parents=[]):
    parser = __cmd_parser.add_parser(command, help=helpmsg, parents=parents)
    parser.set_defaults(command=command)
    parser.set_defaults(dispatch=dispatch)
    return parser


#
# agent commands
#
__parser = __add_command_parser('agent-status', 'get rozo agent status.', agent_dispatch)
__parser = __add_command_parser('agent-start', 'start a rozo agent.', agent_dispatch)
__parser.add_argument('-p', '--pacemaker', action="store_true", default=False, help='when exportd is managed thru pacemaker.')
__parser.add_argument('listeners', nargs='+', choices=[EXPORTD_MANAGER, STORAGED_MANAGER, ROZOFSMOUNT_MANAGER], help='list of listeners.')
__parser = __add_command_parser('agent-stop', 'stop a rozo agent.', agent_dispatch)
__parser = __add_command_parser('agent-restart', 'restart a rozo agent.', agent_dispatch)
__parser.add_argument('-p', '--pacemaker', action="store_true", default=False, help='when exportd is managed thru pacemaker.')
__parser.add_argument('listeners', nargs='+', choices=[EXPORTD_MANAGER, STORAGED_MANAGER, ROZOFSMOUNT_MANAGER], help='list of listeners.')


__parent = argparse.ArgumentParser(add_help=False)
__parent.add_argument('-E', '--exportd', default=socket.gethostname(), help='running platform agent host')


#
# platform related commands
#


#
# __parser = __add_command_parser('set-exportd', 'set exportd hostname.', platform_dispatch, [__parent])
# __parser.add_argument('exportd', nargs=1, help='the exportd hostname to be set.')

__parser = __add_command_parser('nodes', 'display nodes and associated roles.', platform_dispatch, [__parent])
__parser.add_argument('-r', '--roles', nargs='+', choices=STR_ROLES.keys(), help='list of roles to be shown on nodes. If not set all roles will be shown')

# status management
__parser = __add_command_parser('status', 'display nodes status.', platform_dispatch, [__parent])
__parser.add_argument('-n', '--nodes', nargs='+', help='list of nodes to be shown. If not set all nodes will be shown')
__parser.add_argument('-r', '--roles', nargs='+', choices=STR_ROLES.keys(), help='list of roles to be shown on nodes. If not set all roles will be shown')

__parser = __add_command_parser('start', 'start nodes.', platform_dispatch, [__parent])
__parser.add_argument('-n', '--nodes', nargs='+', help='list of nodes to be started. If not set all nodes will be started')
__parser.add_argument('-r', '--roles', nargs='+', choices=STR_ROLES.keys(), help='list of roles to be started on nodes. If not set all roles will be started')

__parser = __add_command_parser('stop', 'stop nodes.', platform_dispatch, [__parent])
__parser.add_argument('-n', '--nodes', nargs='+', help='list of nodes to be stopped. If not set all nodes will be stopped')
__parser.add_argument('-r', '--roles', nargs='+', choices=STR_ROLES.keys(), help='list of roles to be stopped on nodes. If not set all roles will be stopped')

__parser = __add_command_parser('config', 'display nodes configurations.', platform_dispatch, [__parent])
__parser.add_argument('-n', '--nodes', nargs='+', help='list of nodes to be displayed. If not set all nodes will be displayed')
__parser.add_argument('-r', '--roles', nargs='+', choices=STR_ROLES.keys(), help='list of roles to be displayed for each nodes. If not set all roles will be displayed')

__parser = __add_command_parser('profile', 'display nodes profiling.', platform_dispatch, [__parent])
__parser.add_argument('-n', '--nodes', nargs='+', help='list of nodes to be displayed. If not set all nodes will be displayed')
__parser.add_argument('-r', '--roles', nargs='+', choices=STR_ROLES.keys(), help='list of roles to be displayed for each nodes. If not set all roles will be displayed')

__parser = __add_command_parser('layout', 'set platform layout.', platform_dispatch, [__parent])
__parser.add_argument('layout', nargs=1, type=int, choices=[0, 1, 2], help='the layout to set.')


#
# storage related commands
#

__parser = __add_command_parser('stat', 'display platform stats.', platform_dispatch, [__parent])
__parser.add_argument('-v', '--vids', nargs='+', type=int, help='volume(s) to stat. If not set all volumes will be displayed')

__parser = __add_command_parser('expand', 'add nodes to the platform.', platform_dispatch, [__parent])
__parser.add_argument('-v', '--vid', type=int, help='vid of an existing volume. If not set a new volume will be created')
__parser.add_argument('hosts', nargs='+', help='list of nodes to be added.')

__parser = __add_command_parser('shrink', 'remove volume(s) from the platform.', platform_dispatch, [__parent])
__parser.add_argument('vids', nargs='+', type=int, help='vid(s) of existing volume.')

__parser = __add_command_parser('export', 'export a new file system.', platform_dispatch, [__parent])
__parser.add_argument('-n', '--name', default=None, help='Name of this export.')
__parser.add_argument('-p', '--passwd', default=None, help='password to set.')
__parser.add_argument('-s', '--squota', default="", help='soft quota to set.')
__parser.add_argument('-a', '--hquota', default="", help='hard quota to set.')
__parser.add_argument('vid', nargs=1, type=int, help='vid of an existing volume.')

__parser = __add_command_parser('update', 'modify an exported file system.', platform_dispatch, [__parent])
__parser.add_argument('-p', '--passwd', default=None, help='password to set.')
__parser.add_argument('-s', '--squota', default=None, help='soft quota to set.')
__parser.add_argument('-a', '--hquota', default=None, help='hard quota to set.')
__parser.add_argument('eid', nargs=1, type=int, help='eid of an existing export.')

__parser = __add_command_parser('unexport', 'remove exported file system(s).', platform_dispatch, [__parent])
__parser.add_argument('-f', '--force', action="store_true", default=False, help='when ever to force removing.')
__parser.add_argument('eids', nargs='*', type=int, default=None, help='eid(s) of existing export.')

__parser = __add_command_parser('mount', 'mount exported file system(s).', platform_dispatch, [__parent])
__parser.add_argument('-n', '--nodes', nargs='+', help='list of nodes to mount on')
__parser.add_argument('eids', nargs='*', type=int, default=None, help='eid(s) to be mount.')

__parser = __add_command_parser('umount', 'umount exported file system(s).', platform_dispatch, [__parent])
__parser.add_argument('-n', '--nodes', nargs='+', help='list of nodes to umount from')
__parser.add_argument('eids', nargs='*', type=int, default=None, help='eid(s) to be umount.')

# profiler management


def parse(args):
    return __main_parser.parse_args(args)
