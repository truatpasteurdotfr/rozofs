# -*- coding: utf-8 -*-

import Pyro.core
import subprocess
import time

from rozofs.core.constants import STORAGED_MANAGER, EXPORTD_MANAGER, \
    AGENT_PORT, LAYOUT_VALUES, LAYOUT_SAFE, EXPORTS_ROOT, \
    STORAGES_ROOT, ROZOFSMOUNT_MANAGER
from rozofs.core.agent import ServiceStatus
from rozofs.core.exportd import VolumeConfig, ClusterConfig, ExportConfig
from rozofs.core.storaged import StorageConfig
from rozofs.core.profile import ep_client_t, ep_client_initialize, sp_client_t, \
    sp_client_initialize, mp_client_t, mp_client_initialize, ep_client_release, \
    epp_profiler_t, ep_client_get_profiler, sp_client_release, spp_profiler_t, \
    sp_client_get_profiler, mp_client_release, mpp_profiler_t, \
    EppVstatArray_getitem, EppSstatArray_getitem, EppEstatArray_getitem, \
    Uint64Array_getitem, mp_client_get_profiler, Uint16Array_getitem, \
    new_Uint64Array, Uint64Array_setitem, delete_Uint64Array
from rozofs.core.rozofsmount import RozofsMountConfig
from socket import socket

def get_proxy(host, manager):
    try:
        return Pyro.core.getProxyForURI('PYROLOC://%s:%s/%s' % (host, str(AGENT_PORT), manager))
    except:
        raise Exception("no agent %s reachable for %s" % (manager, host))

class Role(object):
    EXPORTD = 1
    STORAGED = 2
    ROZOFSMOUNT = 4
    ROLES = [1, 2, 4]


class ExportStat(object):
    def __init__(self, eid= -1, vid= -1, bsize= -1, blocks= -1, bfree= -1, files= -1, ffree= -1):
        self.eid = eid
        self.vid = vid
        self.bsize = bsize
        self.blocks = blocks
        self.bfree = bfree
        self.files = files
        self.ffree = ffree

class StorageStat(object):
    def __init__(self, sid= -1, status= -1, size= -1, free= -1):
        self.sid = sid
        self.status = status
        self.size = size
        self.free = free

class VolumeStat(object):
    def __init__(self, vid= -1, bsize= -1, bfree= -1, sstats=[]):
        self.vid = vid
        self.bsize = bsize
        self.bfree = bfree
        self.sstats = sstats

class ExportdProfiler(object):
    def __init__(self):
        self.uptime = -1
        self.now = -1
        self.vers = '0.0.0'
        self.vstats = []
        self.estats = []

# ## Rozodebug will do the job
#         self.ep_mount = []
#         self.ep_umount = []
#         self.ep_statfs = []
#         self.ep_lookup = []
#         self.ep_getattr = []
#         self.ep_setattr = []
#         self.ep_readlink = []
#         self.ep_mknod = []
#         self.ep_mkdir = []
#         self.ep_unlink = []
#         self.ep_rmdir = []
#         self.ep_symlink = []
#         self.ep_rename = []
#         self.ep_readdir = []
#         self.ep_read_block = []
#         self.ep_write_block = []
#         self.ep_link = []
#         self.ep_setxattr = []
#         self.ep_getxattr = []
#         self.ep_removexattr = []
#         self.ep_listxattr = []
#         self.export_lv1_resolve_entry = []
#         self.export_lv2_resolve_path = []
#         self.export_lookup_fid = []
#         self.export_update_files = []
#         self.export_update_blocks = []
#         self.export_stat = []
#         self.export_lookup = []
#         self.export_getattr = []
#         self.export_setattr = []
#         self.export_link = []
#         self.export_mknod = []
#         self.export_mkdir = []
#         self.export_unlink = []
#         self.export_rmdir = []
#         self.export_symlink = []
#         self.export_readlink = []
#         self.export_rename = []
#         self.export_read = []
#         self.export_read_block = []
#         self.export_write_block = []
#         self.export_setxattr = []
#         self.export_getxattr = []
#         self.export_removexattr = []
#         self.export_listxattr = []
#         self.export_readdir = []
#         self.lv2_cache_put = []
#         self.lv2_cache_get = []
#         self.lv2_cache_del = []
#         self.volume_balance = []
#         self.volume_distribute = []
#         self.volume_stat = []
#         self.mdir_open = []
#         self.mdir_close = []
#         self.mdir_read_attributes = []
#         self.mdir_write_attributes = []
#         self.mreg_open = []
#         self.mreg_close = []
#         self.mreg_read_attributes = []
#         self.mreg_write_attributes = []
#         self.mreg_read_dist = []
#         self.mreg_write_dist = []
#         self.mslnk_open = []
#         self.mslnk_close = []
#         self.mslnk_read_attributes = []
#         self.mslnk_write_attributes = []
#         self.mslnk_read_link = []
#         self.mslnk_write_link = []


class RpcProxy(object):
    def __init__(self, host, port=0):
        self._host = host
        self._port = port
        self._connect()

    def __del__(self):
        self._disconnect()

    def _connect(self):
        raise NotImplementedError()

    def _disconnect(self):
        raise NotImplementedError()

    def get_profiler(self):
        raise NotImplementedError()


class ExportdRpcProxy(RpcProxy):
    def __init__(self, host, port=0):
        RpcProxy.__init__(self, host, port)

    def _connect(self):
        self._proxy = ep_client_t()
        self._proxy.host = self._host
        self._proxy.port = self._port
        self._proxy.timeout = 10
        ep_client_initialize(self._proxy)

    def _disconnect(self):
        ep_client_release(self._proxy)

    def get_profiler(self):
        p = epp_profiler_t()
        if ep_client_get_profiler(self._proxy, p) != 0:
            raise Exception("can't get exportd profiler.")

        ep = ExportdProfiler()
        ep.uptime = p.uptime
        ep.now = p.now
        ep.vers = p.vers
        for i in range(p.nb_volumes):
            eva = EppVstatArray_getitem(p.vstats, i)
            sstats = []
            for j in range(eva.nb_storages):
                esa = EppSstatArray_getitem(eva.sstats, j)
                sstats.append(StorageStat(esa.sid, esa.status, esa.size, esa.free))
            ep.vstats.append(VolumeStat(eva.vid, eva.bsize, eva.bfree, sstats))
        for i in range(p.nb_exports):
            esa = EppEstatArray_getitem(p.estats, i)
            ep.estats.append(ExportStat(esa.eid, esa.vid, esa.bsize, esa.blocks,
                                        esa.bfree, esa.files, esa.ffree))

# ## Rozodebug will do the job
#        ep.export_read = [Uint64Array_getitem(p.export_read, 0),
#                      Uint64Array_getitem(p.export_read, 1),
#                      Uint64Array_getitem(p.export_read, 2)]
#        ep.ep_read_block = [Uint64Array_getitem(p.ep_read_block, 0),
#                            Uint64Array_getitem(p.ep_read_block, 1),
#                            Uint64Array_getitem(p.ep_read_block, 2)]
#        ep.ep_write_block = [Uint64Array_getitem(p.ep_write_block, 0),
#                             Uint64Array_getitem(p.ep_write_block, 1),
#                             Uint64Array_getitem(p.ep_write_block, 2)]
#
#        for op in ['ep_mount', 'ep_umount', 'ep_statfs', 'ep_lookup', 'ep_getattr',
#                   'ep_setattr', 'ep_readlink', 'ep_mknod', 'ep_mkdir', 'ep_unlink',
#                   'ep_rmdir', 'ep_symlink', 'ep_rename', 'ep_readdir',
#                   'ep_link', 'ep_setxattr', 'ep_getxattr', 'ep_removexattr',
#                   'ep_listxattr', 'export_lv1_resolve_entry', 'export_lv2_resolve_path',
#                   'export_lookup_fid', 'export_update_files', 'export_update_blocks',
#                   'export_stat', 'export_lookup', 'export_getattr', 'export_setattr',
#                   'export_link', 'export_mknod', 'export_mkdir', 'export_unlink', 'export_rmdir',
#                   'export_symlink', 'export_readlink', 'export_rename',
#                   'export_read_block', 'export_write_block',
#                   'export_setxattr', 'export_getxattr', 'export_removexattr',
#                   'export_listxattr', 'export_readdir', 'lv2_cache_put',
#                   'lv2_cache_get', 'lv2_cache_del', 'volume_balance',
#                   'volume_distribute', 'volume_stat', 'mdir_open', 'mdir_close',
#                   'mdir_read_attributes', 'mdir_write_attributes', 'mreg_open',
#                   'mreg_close', 'mreg_read_attributes', 'mreg_write_attributes',
#                   'mreg_read_dist', 'mreg_write_dist', 'mslnk_open', 'mslnk_close',
#                   'mslnk_read_attributes', 'mslnk_write_attributes',
#                   'mslnk_read_link', 'mslnk_write_link']:
#            setattr(ep, op, [Uint64Array_getitem(getattr(p, op), 0),
#                             Uint64Array_getitem(getattr(p, op), 1)])

        return ep


# ## Rozodebug will do the job
# class StoragedProfiler(object):
#    def __init__(self):
#        self.uptime = -1
#        self.now = -1
#        self.vers = '0.0.0'
#        self.stat = []
#        self.ports = []
#        self.remove = []
#        self.read = []
#        self.write = []
#        self.truncate = []
#        self.io_process_ports = []
#        self.rb_process_ports = [];
#        self.rbs_cids = [];
#        self.rbs_sids = [];
#        self.rb_files_current = -1
#        self.rb_files_total = -1
#
#
# class StoragedRpcProxy(RpcProxy):
#    def __init__(self, host, port=0):
#        RpcProxy.__init__(self, host, port)
#
#    def _connect(self):
#        self._proxy = sp_client_t()
#        self._proxy.host = self._host
#        self._proxy.port = self._port
#        self._proxy.timeout = 10
#        sp_client_initialize(self._proxy)
#
#    def _disconnect(self):
#        sp_client_release(self._proxy)
#
#    def get_profiler(self):
#        p = spp_profiler_t()
#
#        if sp_client_get_profiler(self._proxy, p) != 0:
#            raise Exception("can't get storaged profiler.")
#
#        sp = StoragedProfiler()
#        sp.uptime = p.uptime
#        sp.now = p.now
#        sp.vers = p.vers
#
#        sp.stat = [Uint64Array_getitem(p.stat, 0),
#                   Uint64Array_getitem(p.stat, 1)]
#        sp.ports = [Uint64Array_getitem(p.ports, 0),
#                    Uint64Array_getitem(p.ports, 1)]
#        sp.remove = [Uint64Array_getitem(p.remove, 0),
#                    Uint64Array_getitem(p.remove, 1)]
#        sp.io_process_ports = []
#        for ports in range(0, p.nb_io_processes):
#            sp.io_process_ports.append(Uint16Array_getitem(p.io_process_ports, ports))
#        sp.read = [Uint64Array_getitem(p.read, 0),
#                   Uint64Array_getitem(p.read, 1),
#                   Uint64Array_getitem(p.read, 2)]
#        sp.write = [Uint64Array_getitem(p.write, 0),
#                    Uint64Array_getitem(p.write, 1),
#                    Uint64Array_getitem(p.write, 2)]
#        sp.truncate = [Uint64Array_getitem(p.truncate, 0),
#                       Uint64Array_getitem(p.truncate, 1),
#                       Uint64Array_getitem(p.truncate, 2)]
#        sp.rb_process_ports = []
#        for ports in range(0, p.nb_rb_processes):
#            sp.rb_process_ports.append(Uint16Array_getitem(p.rb_process_ports, ports))
#        sp.rb_files_current = p.rb_files_current
#        sp.rb_files_total = p.rb_files_total
#
#        return sp
#
#
# class MountProfiler(object):
#    def __init__(self):
#        self.uptime = -1
#        self.now = -1
#        self.vers = '0.0.0'
#        self.rozofs_ll_lookup = []
#        self.rozofs_ll_forget = []
#        self.rozofs_ll_getattr = []
#        self.rozofs_ll_setattr = []
#        self.rozofs_ll_readlink = []
#        self.rozofs_ll_mknod = []
#        self.rozofs_ll_mkdir = []
#        self.rozofs_ll_unlink = []
#        self.rozofs_ll_rmdir = []
#        self.rozofs_ll_symlink = []
#        self.rozofs_ll_rename = []
#        self.rozofs_ll_open = []
#        self.rozofs_ll_link = []
#        self.rozofs_ll_read = []
#        self.rozofs_ll_write = []
#        self.rozofs_ll_flush = []
#        self.rozofs_ll_release = []
#        self.rozofs_ll_opendir = []
#        self.rozofs_ll_readdir = []
#        self.rozofs_ll_releasedir = []
#        self.rozofs_ll_fsyncdir = []
#        self.rozofs_ll_statfs = []
#        self.rozofs_ll_setxattr = []
#        self.rozofs_ll_getxattr = []
#        self.rozofs_ll_listxattr = []
#        self.rozofs_ll_removexattr = []
#        self.rozofs_ll_access = []
#        self.rozofs_ll_create = []
#        self.rozofs_ll_getlk = []
#        self.rozofs_ll_setlk = []
#        self.rozofs_ll_ioctl = []
#
#
# class MountRpcProxy(RpcProxy):
#    def __init__(self, host, port=0):
#        RpcProxy.__init__(self, host, port)
#
#    def _connect(self):
#        self._proxy = mp_client_t()
#        self._proxy.host = self._host
#        self._proxy.port = self._port
#        self._proxy.timeout = 10
#        mp_client_initialize(self._proxy)
#
#    def _disconnect(self):
#        mp_client_release(self._proxy)
#
#    def get_profiler(self):
#        p = mpp_profiler_t()
#        if mp_client_get_profiler(self._proxy, p) != 0:
#            raise Exception("can't get mount profiler.")
#
#        mp = MountProfiler()
#        mp.uptime = p.uptime
#        mp.now = p.now
#        mp.vers = p.vers
#
#        mp.rozofs_ll_read = [Uint64Array_getitem(p.rozofs_ll_read, 0),
#                               Uint64Array_getitem(p.rozofs_ll_read, 1),
#                               Uint64Array_getitem(p.rozofs_ll_read, 2)]
#        mp.rozofs_ll_write = [Uint64Array_getitem(p.rozofs_ll_write, 0),
#                               Uint64Array_getitem(p.rozofs_ll_write, 1),
#                               Uint64Array_getitem(p.rozofs_ll_write, 2)]
#        for op in ["rozofs_ll_lookup", "rozofs_ll_forget", "rozofs_ll_getattr",
#                   "rozofs_ll_setattr", "rozofs_ll_readlink", "rozofs_ll_mknod",
#                   "rozofs_ll_mkdir", "rozofs_ll_unlink", "rozofs_ll_rmdir",
#                   "rozofs_ll_symlink", "rozofs_ll_rename", "rozofs_ll_open",
#                   "rozofs_ll_link", "rozofs_ll_flush", "rozofs_ll_release",
#                   "rozofs_ll_opendir", "rozofs_ll_readdir", "rozofs_ll_releasedir",
#                   "rozofs_ll_fsyncdir", "rozofs_ll_statfs", "rozofs_ll_setxattr",
#                   "rozofs_ll_getxattr", "rozofs_ll_listxattr", "rozofs_ll_removexattr",
#                   "rozofs_ll_access", "rozofs_ll_create", "rozofs_ll_getlk",
#                   "rozofs_ll_setlk", "rozofs_ll_ioctl"]:
#            setattr(mp, op, [Uint64Array_getitem(getattr(p, op), 0),
#                             Uint64Array_getitem(getattr(p, op), 1)])
#
#        return mp

class Node(object):

    def __init__(self, host, roles=0):
        self._host = host
        self._roles = roles
        # for all below key is Role
        self._proxies = {}
        # self._rpcproxies = {}  # storaged ares arrays dues to io & rb processes
        # does our node is connected and running
        self._up = False
        # self._connected = False

    def __del(self):
        self._release_proxies()

    def _initialize_proxies(self):
        try:
            if self.has_roles(Role.EXPORTD) and Role.EXPORTD not in self._proxies:
                self._proxies[Role.EXPORTD] = get_proxy(self._host, EXPORTD_MANAGER)
            if self.has_roles(Role.STORAGED) and Role.STORAGED not in self._proxies:
                self._proxies[Role.STORAGED] = get_proxy(self._host, STORAGED_MANAGER)
            if self.has_roles(Role.ROZOFSMOUNT) and Role.ROZOFSMOUNT not in self._proxies:
                self._proxies[Role.ROZOFSMOUNT] = get_proxy(self._host, ROZOFSMOUNT_MANAGER)
        except Exception as e:
            self._release_proxies()
            raise e

    def _release_proxies(self):
        for p in self._proxies.values():
            p.getProxy()._release()
        self._up = False

#    def _connect_rpc(self):
#        try:
#            if self.has_roles(Role.EXPORTD) and Role.EXPORTD not in self._rpcproxies:
#                if self._proxies[Role.EXPORTD].get_service_status() == ServiceStatus.STOPPED:
#                    self._rpcproxies[Role.EXPORTD] = None
#                else:
#                    self._rpcproxies[Role.EXPORTD] = ExportdRpcProxy(self._host)
#            if self.has_roles(Role.STORAGED) and Role.STORAGED not in self._rpcproxies:
#                if self._proxies[Role.STORAGED].get_service_status() == ServiceStatus.STOPPED:
#                    self._rpcproxies[Role.STORAGED] = None
#                else:
#                    self._rpcproxies[Role.STORAGED] = []
#                    self._rpcproxies[Role.STORAGED].append(StoragedRpcProxy(self._host))
#                    sp = self._rpcproxies[Role.STORAGED][0].get_profiler()
#                    for port in sp.io_process_ports:
#                        self._rpcproxies[Role.STORAGED].append(StoragedRpcProxy(self._host, port))
#                    for port in sp.rb_process_ports:
#                        try:
#                            # on error rebuild processes might be finished
#                            self._rpcproxies[Role.STORAGED].append(StoragedRpcProxy(self._host, port))
#                        except:
#                            continue
#            if self.has_roles(Role.ROZOFSMOUNT) and Role.ROZOFSMOUNT not in self._rpcproxies:
#                if self._proxies[Role.ROZOFSMOUNT].get_service_status() == ServiceStatus.STOPPED:
#                    self._rpcproxies[Role.ROZOFSMOUNT] = None
#                else:
#                    self._rpcproxies[Role.ROZOFSMOUNT] = []
#                    sc = self._proxies[Role.ROZOFSMOUNT].get_service_config()
#                    for port in [s.profiling_port for s in sc]:
#                        self._rpcproxies[Role.ROZOFSMOUNT].append(MountRpcProxy(self._host, port))
#        except Exception as e:
#            self._disconnect_rpc()
#            raise e
#
#    def _disconnect_rpc(self):
#        for k in self._proxies.keys():
#            del self._proxies[k]
#        self._connected = False

    def _try_up(self):
        """ check if a node is up if not try to set it up """
        if self._up == False:
            # is it reachable ?
            with open('/dev/null', 'w') as devnull:
                if subprocess.call(['ping', '-c', '1', self._host], stdout=devnull,
                    stderr=devnull) is not 0:
                        return False
            # yes, so try to connect to agents
            try:
                self._initialize_proxies()
            except:
                return False
            self._up = True

        return True

#    def _try_connect(self):
#        """ check if a node has running rpc service and connect to it """
#        if not self._try_up():
#            return False
#
#        if self._connected == False:
#            try:
#                self._connect_rpc()
#            except:
#                return False
#            self._connected = True
#
#        return True

    def is_up(self):
        return self._up

    def set_roles(self, roles):
        self._roles = roles
        if self._up:
            self._release_proxies()

    def get_roles(self):
        return self._roles

    def has_roles(self, roles):
        return self._roles & roles == roles

    def has_one_of_roles(self, roles):
        for role in [r for r in Role.ROLES if r & roles == r]:
            if self._roles & role == role:
                return True
        return False

    def get_configurations(self, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
        if not self._try_up():
            return None

        configurations = {}
        for role in [r for r in Role.ROLES if r & roles == r and self.has_roles(r)]:
            configurations[role] = self._proxies[role].get_service_config()

        return configurations

    def set_configurations(self, configurations):
        if not self._try_up():
            return

        for r, c in configurations.items():
            if self.has_roles(r):
                self._proxies[r].set_service_config(c)

    def get_statuses(self, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
        if not self._try_up():
            return None

        statuses = {}
        for role in [r for r in Role.ROLES if r & roles == r and self.has_roles(r)]:
            statuses[role] = self._proxies[role].get_service_status()

        return statuses

#    def get_profilers(self, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
#        if not self._try_connect():
#            return None
#
#        profilers = {}
#        for role in [r for r in Role.ROLES if r & roles == r and self.has_roles(r)]:
#            if role == Role.EXPORTD:
#                if self._rpcproxies[role] is not None:
#                    profilers[Role.EXPORTD] = self._rpcproxies[role].get_profiler()
#                else:
#                    profilers[Role.EXPORTD] = None
#            else:
#                if self._rpcproxies[role] is not None:
#                    profilers[role] = []
#                    for p in self._rpcproxies[role]:
#                        profilers[role].append(p.get_profiler())
#                else:
#                    profilers[role] = None
#        return profilers

    def set_statuses(self, statuses):
        if not self._try_up():
            return
        for r, s in statuses.items():
            if self.has_roles(r):
                self._proxies[r].set_service_status(s)


class Platform(object):
    """ A rozofs platform."""

    def __init__(self, hostname="localhost"):
        """
        Args:
            hostname: platform manager host (should be part of the platform !).
        """
        self._hostname = hostname
        self._nodes = self._get_nodes(hostname)

    def _get_nodes(self, hostname):
        nodes = {}

        exportd_node = Node(hostname, Role.EXPORTD)

        nodes[hostname] = exportd_node

        node_configs = exportd_node.get_configurations(Role.EXPORTD)
        if not node_configs:
            raise Exception("%s unreachable." % hostname)

        econfig = node_configs[Role.EXPORTD]

        if econfig is None:
            raise "Exportd node is off line."

        for h in [s for v in econfig.volumes.values()
                        for c in v.clusters.values()
                        for s in c.storages.values()]:
            roles = Role.STORAGED
            # check if has rozofsmount
            try :
                p = get_proxy(h, ROZOFSMOUNT_MANAGER)
                if p.get_service_config():
                    roles = roles | Role.ROZOFSMOUNT
            except:
                continue
            if h in nodes:  # the exportd node !
                nodes[h].set_roles(nodes[h].get_roles() | roles)
            else:
                nodes[h] = Node(h, roles)

        return nodes

    def _check_nodes(self):
        """ Check if all nodes have a platform agent running """
        for n in self._nodes:
            if not n.check_platform_manager():
                raise Exception("%s: check platform failed." % n._host)


    # the first exportd nodes
    def _get_exportd_node(self):
        for n in self._nodes.values():
            if n.has_roles(Role.EXPORTD):
                return n

    def list_nodes(self, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
        """ Get all nodes managed by this platform

        Args:
            roles: roles that node should have

        Return:
            A dict: keys are host names, values are roles (as xored flags)
        """
        nodes = {}
        for h, n in self._nodes.items():
            if n.has_one_of_roles(roles):
                nodes[h] = n.get_roles()
        return nodes

    def get_statuses(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
        """ Get statuses for named nodes and roles

        Args:
            hosts : list of hosts to get statuses from, if None all host are
                    checked

            roles : for which roles statuses should be retrieved
                    if a given host doesn't have this role the return statuses
                    will not contain key for this role (and might be empty).

        Return:
            A dict: keys are host names, values are dicts {Role: ServiceStatus}
                    or None if node is off line
        """
        statuses = {}
        if hosts is None:
            for h, n in self._nodes.items():
                statuses[h] = n.get_statuses(roles)
        else:
            for h in hosts:
                statuses[h] = self._nodes[h].get_statuses(roles)
        return statuses

    def set_statuses(self, statuses):
        """ Set statuses

        Args:
            statuses : A dict where keys are host names,
                       values are dicts {Role: ServiceStatus}

        Warning:
            assuming user knows what he is doing start share before
            exportd or storaged will lead to errors
        """
        for h, s in statuses.items():
            self._nodes[h].set_statuses(s)

    def set_status(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT, status=ServiceStatus.STOPPED):
        """ Convenient method to set the same status to hosts.

        Args:
            hosts: list of hosts to set status , if None all host are set

            roles: for which roles status should be set

        Warning:
            see set_statuses
        """
        # while Node set statuses is applied only if a node has this role
        # we just need to call set statuses on all nodes
        statuses_to_set = {}
        for role in [r for r in Role.ROLES if r & roles == r]:
            statuses_to_set[role] = status
        statuses = {}
        if hosts is None:
            for host in self._nodes.keys():
                statuses[host] = statuses_to_set
        else:
            for host in hosts:
                statuses[host] = statuses_to_set

        self.set_statuses(statuses)

    def start(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
        """ Convenient method to start all nodes with a role
        Args:
            the roles to be started
        """

        # check if hosts are managed
        if hosts is not None:
            for h in hosts:
                if h not in self._nodes.keys():
                    raise Exception('unmanaged host: %s' % h)

        if hosts is None:
            hosts = self._nodes.keys()

        # take care of the starting order
        if roles & Role.STORAGED == Role.STORAGED:
            self.set_status(hosts, Role.STORAGED, ServiceStatus.STARTED)
        if roles & Role.EXPORTD == Role.EXPORTD:
            self.set_status(hosts, Role.EXPORTD, ServiceStatus.STARTED)
        if roles & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT:
            time.sleep(1)
            self.set_status(hosts, Role.ROZOFSMOUNT, ServiceStatus.STARTED)

    def stop(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
        """ Convenient method to stop all nodes with a role
        Args:
            the roles to be stopped
        """
        if hosts is None:
            hosts = self._nodes.keys()

        # take care of the stopping order
        if roles & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT:
            self.set_status(hosts, Role.ROZOFSMOUNT, ServiceStatus.STOPPED)
        if roles & Role.EXPORTD == Role.EXPORTD:
            self.set_status(hosts, Role.EXPORTD, ServiceStatus.STOPPED)
        if roles & Role.STORAGED == Role.STORAGED:
            self.set_status(hosts, Role.STORAGED, ServiceStatus.STOPPED)

#    def get_profilers(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
#        """ Get profilers for named nodes and roles
#
#        Args:
#            hosts : list of hosts to get profiles from, if None all host are
#                    checked
#
#            roles : for which roles statuses should be retrieved
#                    if a given host doesn't have this role the return profiles
#                    will not contain key for this role.
#
#        Return:
#            A dict: keys are host names, values are dicts {Role: profile}
#                    or None if node is off line. profile arrays in case
#                    of storaged or share roles
#        """
#        profilers = {}
#        if hosts is None:
#            for h, n in self._nodes.items():
#                profilers[h] = n.get_profilers(roles)
#        else:
#            for h in hosts:
#                profilers[h] = self._nodes[h].get_profilers(roles)
#
#        return profilers

    def stat(self):
       """ Get statistics from exportd """
       return ExportdRpcProxy(self._hostname).get_profiler()


    def get_configurations(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT):
        """ Get configurations for named nodes and roles

        Args:
            hosts : list of hosts to get statuses from, if None all host are
                    checked

            roles : for which roles statuses should be retrieved
                    if a given host doesn't have this role the return statuses
                    will not contain key for this role.

        Return:
            A dict: keys are host names, values are dicts {Role: configuration}
                    or None if node is off line.
        """
        configurations = {}
        if hosts is None:
            for h, n in self._nodes.items():
                configurations[h] = n.get_configurations(roles)
        else:
            for h in hosts:
                configurations[h] = self._nodes[h].get_configurations(roles)

        return configurations

    def set_layout(self, layout):
        if not layout in [0, 1, 2]:
            raise Exception("invalid layout: %d" % layout)

        node = self._get_exportd_node()
        configuration = node.get_configurations(Role.EXPORTD)

        if configuration is None:
            raise "exportd node is off line."

        if len(configuration[Role.EXPORTD].volumes) != 0:
            raise Exception("platform has configured volume(s) !!!")

        configuration[Role.EXPORTD].layout = layout
        node.set_configurations(configuration)

    def get_layout(self):
        node = self._get_exportd_node()
        configuration = node.get_configurations(Role.EXPORTD)

        if configuration is None:
            raise "exportd node is off line."

        return configuration[Role.EXPORTD].layout

    def add_nodes(self, hosts, vid=None):
        """ Add storaged nodes to the platform

        Args:
            vid: volume to use, if none a new one will be created
            hosts: hosts to be added
        """
        enode = self._get_exportd_node()
        econfig = enode.get_configurations(Role.EXPORTD)

        if econfig is None:
            raise Exception("Exportd node is off line.")

        if len(hosts) < LAYOUT_VALUES[econfig[Role.EXPORTD].layout][LAYOUT_SAFE]:
            raise Exception("too few hosts: %s over %s" % (len(hosts),
                    LAYOUT_VALUES[econfig[Role.EXPORTD].layout][LAYOUT_SAFE]))

        if vid is not None:
            # check if the given one is a new one else get the related VolumeConfig
            if vid in econfig[Role.EXPORTD].volumes:
                vconfig = econfig[Role.EXPORTD].volumes[vid]
            else:
                vconfig = VolumeConfig(vid)
        else:
            vids = econfig[Role.EXPORTD].volumes.keys()
            if len(vids) != 0 :
                vid = max(vids) + 1
            else:
                vid = 1
            vconfig = VolumeConfig(vid)

        # find a cluster id
        cids = [c.cid for v in econfig[Role.EXPORTD].volumes.values()
                      for c in v.clusters.values()]
        if len(cids) != 0:
            cid = max(cids) + 1
        else:
            cid = 1

        # as we create a new cluster sids always starts at 1
        sid = 1

        cconfig = ClusterConfig(cid)
        roles = Role.STORAGED

        for h in hosts:
            # if it's a new node register it (and duplicate platform config)
            # else just update its role
            if h not in self._nodes:
                self._nodes[h] = Node(h, roles)
            else:
                # maybe overkill since the node could already have this role
                self._nodes[h].set_roles(self._nodes[h].get_roles() | roles)

            # configure the storaged
            sconfig = self._nodes[h].get_configurations(Role.STORAGED)
            # XXX root could (should ?) be compute on storaged module
            sconfig[Role.STORAGED].storages[(cid, sid)] = StorageConfig(cid,
                 sid, "%s/storage_%d_%d" % (STORAGES_ROOT, cid, sid))
            self._nodes[h].set_configurations(sconfig)

            # add the storage to the cluster
            cconfig.storages[sid] = h
            sid += 1

        # add the cluster to the volume
        vconfig.clusters[cid] = cconfig
        # update the exportd config
        econfig[Role.EXPORTD].volumes[vid] = vconfig
        enode.set_configurations(econfig)

    def remove_volume(self, vid):
        """ Remove a volume

        Args:
            vid: the volume to be removed
        """
        enode = self._get_exportd_node()
        econfig = enode.get_configurations(Role.EXPORTD)

        if econfig is None:
            raise Exception("Exportd node is off line.")

        if vid not in econfig[Role.EXPORTD].volumes:
            raise Exception("Unknown volume: %d." % vid)

        if [e for e in econfig[Role.EXPORTD].exports.values() if e.vid == vid]:
            raise Exception("Volume has configured export(s)")
        else:
            vconfig = econfig[Role.EXPORTD].volumes.pop(vid)
            for c in vconfig.clusters.values():
                for sid, host in c.storages.items():
                    sconfig = self._nodes[host].get_configurations(Role.STORAGED)
                    sconfig[Role.STORAGED].storages.pop((c.cid, sid))
                    self._nodes[host].set_configurations(sconfig)
            enode.set_configurations(econfig)

    def create_export(self, vid, name=None, passwd=None, squota="", hquota=""):
        """ Export a new file system

        Args:
            vid: the volume id to use the file system relies on
        """
        # exportd_hostname = self.get_exportd_hostname()
        enode = self._get_exportd_node()
        econfig = enode.get_configurations(Role.EXPORTD)

        if econfig is None:
            raise Exception("Exportd node is off line.")

        if vid not in econfig[Role.EXPORTD].volumes:
            raise Exception("Unknown volume: %d." % vid)

        # validate quotas
        if squota:
            if not squota[0].isdigit():
                raise Exception("Invalid squota format: %s." % squota)

            for c in squota:
                if not c.isdigit():
                    if c not in ['K', 'M', 'G']:
                        raise Exception("Invalid squota format: %s." % squota)
                    else:
                        break

        if hquota:
            if not hquota[0].isdigit():
                raise Exception("Invalid hquota format: %s." % hquota)


            for c in hquota:
                if not c.isdigit():
                    if c not in ['K', 'M', 'G']:
                        raise Exception("Invalid hquota format: %s." % squota)
                    else:
                        break

        # find an eid
        eids = econfig[Role.EXPORTD].exports.keys()
        if len(eids) != 0:
            eid = max(eids) + 1
        else:
            eid = 1

        # check name
        if name is None:
            name = "export_%d" % eid
        else:
            if "%s/%s" % (EXPORTS_ROOT, name) in [e.root for e in econfig[Role.EXPORTD].exports.values()]:
                raise Exception("Duplicate export name: %s" % name)

        # compute md5
        if passwd is None:
            md5 = ""
        else:
            with open('/dev/null', 'w') as devnull:
                md5 = subprocess.check_output(['md5pass', passwd, 'rozofs'],
                                   stderr=devnull)[11:].rstrip()

        # add this new export
        econfig[Role.EXPORTD].exports[eid] = ExportConfig(eid, vid, "%s/%s" % (EXPORTS_ROOT, name), md5, squota, hquota)
        enode.set_configurations(econfig)

    def update_export(self, eid, current=None, passwd=None, squota=None, hquota=None):
        """ Modify an exported file system

        Args:
            eid: the export id to modify
            current: current password (only need if passwd)
            passwd: password to set if None no modification is done
            squota: soft quota to set if None no modification is done
            hquota: hard quota to set if None no modification is done
        """
        enode = self._get_exportd_node()
        econfig = enode.get_configurations(Role.EXPORTD)

        if econfig is None:
            raise Exception("Exportd node is off line.")

        if eid not in econfig[Role.EXPORTD].exports.keys():
            raise Exception("Unknown export: %d." % eid)

        # validate quotas
        if squota is not None:
            if squota:
                if not squota[0].isdigit():
                    raise Exception("Invalid squota format: %s." % squota)

                for c in squota:
                    if not c.isdigit():
                        if c not in ['K', 'M', 'G']:
                            raise Exception("Invalid squota format: %s." % squota)
                        else:
                            break
            econfig[Role.EXPORTD].exports[eid].squota = squota

        if hquota is not None:
            if hquota:
                if not hquota[0].isdigit():
                    raise Exception("Invalid hquota format: %s." % hquota)

                for c in hquota:
                    if not c.isdigit():
                        if c not in ['K', 'M', 'G']:
                            raise Exception("Invalid hquota format: %s." % squota)
                        else:
                            break
            econfig[Role.EXPORTD].exports[eid].hquota = hquota

        # compute md5
        if passwd is not None:
            if current is None:
                raise "Current password needed."

            # check current passwd
            with open('/dev/null', 'w') as devnull:
                    if econfig[Role.EXPORTD].exports[eid].md5 != subprocess.check_output(['md5pass', current, 'rozofs'],
                                   stderr=devnull)[11:].rstrip():
                        raise "Permission denied."

            if passwd:
                with open('/dev/null', 'w') as devnull:
                    econfig[Role.EXPORTD].exports[eid].md5 = subprocess.check_output(['md5pass', passwd, 'rozofs'],
                                   stderr=devnull)[11:].rstrip()
            else:
                econfig[Role.EXPORTD].exports[eid].md5 = ""

        # update this export
        enode.set_configurations(econfig)

    def remove_export(self, eids=None, force=False):
        """ Remove exported file systems

        To not keep unused data stored, exports containing files will not be remove
        unless force is set to True.

        Args:
            eids: export ids to remove
            force: force removing of non empty exportd
        """

        enode = self._get_exportd_node()
        econfig = enode.get_configurations(Role.EXPORTD)
        eprofiler = ExportdRpcProxy(self._hostname).get_profiler()

        if econfig is None or eprofiler is None:
            raise Exception("Exportd node is off line.")

        if eids is None:
            eids = econfig[Role.EXPORTD].exports.keys()

        # unmount if needed
        self.umount_export(eids)

        # sanity check
        for eid in eids:
            if eid not in econfig[Role.EXPORTD].exports.keys():
                raise Exception("Unknown export: %d." % eid)

            for estat in eprofiler.estats:
                if estat.eid == eid and estat.files != 0 and not force:
                    raise Exception("Can't remove non empty export (use  force=True)")


        # delete these exports from exportd configuration
        for eid in eids:
            econfig[Role.EXPORTD].exports.pop(eid)

        enode.set_configurations(econfig)

    def mount_export(self, eids=None, hosts=None):
        """ Mount an exported file system

        Only kown (managed by this platform) hosts could mount a file system.

        Args:
            eids: the export ids to mount if None all exports are mount
            hosts: target hosts to mount on if None all export will be mount on
                   all nodes
        """

        enode = self._get_exportd_node()
        econfig = enode.get_configurations(Role.EXPORTD)

        if econfig is None:
            raise Exception("Exportd node is off line.")

        if hosts is None:
            hosts = self._nodes.keys()

        if eids is None:
            eids = econfig[Role.EXPORTD].exports.keys()


        # sanity check
        for h in hosts:
            if h not in self._nodes:
                raise Exception("Unknown host: %s." % h)
        for eid in eids:
            if eid not in econfig[Role.EXPORTD].exports.keys():
                raise Exception("Unknown export: %d." % eid)

        # mount
        for h in hosts:
            node = self._nodes[h]
            # may be overkill
            node.set_roles(node.get_roles() | Role.ROZOFSMOUNT)
            rconfigs = node.get_configurations(Role.ROZOFSMOUNT)

            for eid in eids:
                expconfig = econfig[Role.EXPORTD].exports[eid]
                rconfig = RozofsMountConfig(self._hostname , expconfig.root, -1)
                # check duplicates
                if rconfig not in rconfigs:
                    rconfigs[Role.ROZOFSMOUNT].append(rconfig)

            node.set_configurations(rconfigs)

    def umount_export(self, eids=None, hosts=None):
        """ Umount an exported file system

        Only kown (managed by this platform) hosts could umount a file system.

        Args:
            eids: the export ids to mount if None all exports are umount
            hosts: target hosts to mount on if None all export will be mount on
                   all nodes
        """

        if hosts is None:
            hosts = self._nodes.keys()

        enode = self._get_exportd_node()
        econfig = enode.get_configurations(Role.EXPORTD)

        if econfig is None:
            raise Exception("Exportd node is off line.")

        if eids is None:
            eids = econfig[Role.EXPORTD].exports.keys()

        # sanity check
        for h in hosts:
            if h not in self._nodes:
                raise Exception("Unknown host: %s." % h)
        for eid in eids:
            if eid not in econfig[Role.EXPORTD].exports.keys():
                raise Exception("Unknown export: %d." % eid)

        # umount
        for h in hosts:
            node = self._nodes[h]
            if node.has_one_of_roles(Role.ROZOFSMOUNT):
                rconfigs = node.get_configurations(Role.ROZOFSMOUNT)

                for eid in eids:
                    expconfig = econfig[Role.EXPORTD].exports[eid]
                    rconfig = RozofsMountConfig(self._hostname , expconfig.root, -1)
                    if rconfig in rconfigs[Role.ROZOFSMOUNT]:
                        rconfigs[Role.ROZOFSMOUNT].remove(rconfig)

                node.set_configurations(rconfigs)
                if not rconfigs:
                    node.set_roles(node.get_roles ^ Role.ROZOFSMOUNT)
