# -*- coding: utf-8 -*-

from rozofs.core.constants import STORAGED_MANAGER, EXPORTD_MANAGER, \
    PLATFORM_MANAGER, EXPORTD_HOSTNAME, PROTOCOLS, SHARE_MANAGER, AGENT_PORT, \
    LAYOUT_VALUES, LAYOUT_SAFE, PROTOCOLS_VALUES, SID_MAX
import Pyro.core
from rozofs.core.agent import Agent, ServiceStatus
from rozofs.core.configuration import ConfigurationReader, ConfigurationWriter, \
    ConfigurationParser
from rozofs.core.libconfig import config_setting_add, CONFIG_TYPE_STRING, \
    config_setting_set_string, CONFIG_TYPE_LIST, config_lookup, \
    config_setting_get_string, config_setting_length, config_setting_get_elem
import subprocess
from rozofs.core.exportd import VolumeConfig, ClusterConfig, ExportConfig
from rozofs.core.storaged import StorageConfig
from rozofs.core.profile import ep_client_t, ep_client_initialize, sp_client_t, \
    sp_client_initialize, mp_client_t, mp_client_initialize, ep_client_release, \
    epp_profiler_t, ep_client_get_profiler, sp_client_release, spp_profiler_t, \
    sp_client_get_profiler, mp_client_release, mpp_profiler_t, \
    EppVstatArray_getitem, EppSstatArray_getitem, EppEstatArray_getitem, \
    Uint64Array_getitem, mp_client_get_profiler, Uint16Array_getitem
from rozofs.core.share import ShareConfig, Share
import time

def get_proxy(host, manager):
    try:
        return Pyro.core.getProxyForURI('PYROLOC://%s:%s/%s' % (host, str(AGENT_PORT), manager))
    except:
        raise Exception("no agent %s reachable for %s" % (manager, host))

#
# Plateform Agent
#
class PlatformConfig(object):
    def __init__(self, exportd_hostname="", protocols=[]):
        self.exportd_hostname = exportd_hostname
#        self.exportd_standalone = exportd_standalone
        self.protocols = protocols


class PlatformConfigurationParser(ConfigurationParser):

    def parse(self, configuration, config):
        exportd_hostname_setting = config_setting_add(config.root, EXPORTD_HOSTNAME, CONFIG_TYPE_STRING)
        config_setting_set_string(exportd_hostname_setting, configuration.exportd_hostname)

#        exportd_standalone_setting = config_setting_add(config.root, EXPORTD_STANDALONE, CONFIG_TYPE_BOOL)
#        config_setting_set_bool(exportd_standalone_setting, configuration.exportd_standalone)

        protocol_settings = config_setting_add(config.root, PROTOCOLS, CONFIG_TYPE_LIST)
        for protocol in configuration.protocols:
            protocol_setting = config_setting_add(protocol_settings, '', CONFIG_TYPE_STRING)
            config_setting_set_string(protocol_setting, protocol)

    def unparse(self, config, configuration):
        exportd_hostname_setting = config_lookup(config, EXPORTD_HOSTNAME)
        if exportd_hostname_setting is None:
            raise Exception("Wrong format: no exportd hostname defined.")
        configuration.exportd_hostname = config_setting_get_string(exportd_hostname_setting)

#        exportd_standalone_setting = config_lookup(config, EXPORTD_STANDALONE)
#        if exportd_standalone_setting is None:
#            configuration.exportd_standalone = False
#        else:
#            configuration.exportd_standalone = config_setting_get_bool(exportd_standalone_setting)

        protocol_settings = config_lookup(config, PROTOCOLS)

        configuration.protocols = []
        if protocol_settings is not None:
            for i in range(config_setting_length(protocol_settings)):
                protocol_setting = config_setting_get_elem(protocol_settings, i)
                configuration.protocols.append(config_setting_get_string(protocol_setting))


class PlatformAgent(Agent):

    def __init__(self, config='/etc/rozofs/platform.conf'):
        Agent.__init__(self, PLATFORM_MANAGER)
        self._reader = ConfigurationReader(config, PlatformConfigurationParser())
        self._writer = ConfigurationWriter(config, PlatformConfigurationParser())

    def ping(self):
        return True

    def get_service_config(self):
        configuration = PlatformConfig()
        return self._reader.read(configuration)

    def set_service_config(self, configuration):
        self._writer.write(configuration)


class Role(object):
    EXPORTD = 1
    STORAGED = 2
    SHARE = 4
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
        self.ep_mount = []
        self.ep_umount = []
        self.ep_statfs = []
        self.ep_lookup = []
        self.ep_getattr = []
        self.ep_setattr = []
        self.ep_readlink = []
        self.ep_mknod = []
        self.ep_mkdir = []
        self.ep_unlink = []
        self.ep_rmdir = []
        self.ep_symlink = []
        self.ep_rename = []
        self.ep_readdir = []
        self.ep_read_block = []
        self.ep_write_block = []
        self.ep_link = []
        self.ep_setxattr = []
        self.ep_getxattr = []
        self.ep_removexattr = []
        self.ep_listxattr = []
        self.export_lv1_resolve_entry = []
        self.export_lv2_resolve_path = []
        self.export_lookup_fid = []
        self.export_update_files = []
        self.export_update_blocks = []
        self.export_stat = []
        self.export_lookup = []
        self.export_getattr = []
        self.export_setattr = []
        self.export_link = []
        self.export_mknod = []
        self.export_mkdir = []
        self.export_unlink = []
        self.export_rmdir = []
        self.export_symlink = []
        self.export_readlink = []
        self.export_rename = []
        self.export_read = []
        self.export_read_block = []
        self.export_write_block = []
        self.export_setxattr = []
        self.export_getxattr = []
        self.export_removexattr = []
        self.export_listxattr = []
        self.export_readdir = []
        self.lv2_cache_put = []
        self.lv2_cache_get = []
        self.lv2_cache_del = []
        self.volume_balance = []
        self.volume_distribute = []
        self.volume_stat = []
        self.mdir_open = []
        self.mdir_close = []
        self.mdir_read_attributes = []
        self.mdir_write_attributes = []
        self.mreg_open = []
        self.mreg_close = []
        self.mreg_read_attributes = []
        self.mreg_write_attributes = []
        self.mreg_read_dist = []
        self.mreg_write_dist = []
        self.mslnk_open = []
        self.mslnk_close = []
        self.mslnk_read_attributes = []
        self.mslnk_write_attributes = []
        self.mslnk_read_link = []
        self.mslnk_write_link = []


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

        ep.export_read = [Uint64Array_getitem(p.export_read, 0),
                      Uint64Array_getitem(p.export_read, 1),
                      Uint64Array_getitem(p.export_read, 2)]
        ep.ep_read_block = [Uint64Array_getitem(p.ep_read_block, 0),
                            Uint64Array_getitem(p.ep_read_block, 1),
                            Uint64Array_getitem(p.ep_read_block, 2)]
        ep.ep_write_block = [Uint64Array_getitem(p.ep_write_block, 0),
                             Uint64Array_getitem(p.ep_write_block, 1),
                             Uint64Array_getitem(p.ep_write_block, 2)]

        for op in ['ep_mount', 'ep_umount', 'ep_statfs', 'ep_lookup', 'ep_getattr',
                   'ep_setattr', 'ep_readlink', 'ep_mknod', 'ep_mkdir', 'ep_unlink',
                   'ep_rmdir', 'ep_symlink', 'ep_rename', 'ep_readdir',
                   'ep_link', 'ep_setxattr', 'ep_getxattr', 'ep_removexattr',
                   'ep_listxattr', 'export_lv1_resolve_entry', 'export_lv2_resolve_path',
                   'export_lookup_fid', 'export_update_files', 'export_update_blocks',
                   'export_stat', 'export_lookup', 'export_getattr', 'export_setattr',
                   'export_link', 'export_mknod', 'export_mkdir', 'export_unlink', 'export_rmdir',
                   'export_symlink', 'export_readlink', 'export_rename',
                   'export_read_block', 'export_write_block',
                   'export_setxattr', 'export_getxattr', 'export_removexattr',
                   'export_listxattr', 'export_readdir', 'lv2_cache_put',
                   'lv2_cache_get', 'lv2_cache_del', 'volume_balance',
                   'volume_distribute', 'volume_stat', 'mdir_open', 'mdir_close',
                   'mdir_read_attributes', 'mdir_write_attributes', 'mreg_open',
                   'mreg_close', 'mreg_read_attributes', 'mreg_write_attributes',
                   'mreg_read_dist', 'mreg_write_dist', 'mslnk_open', 'mslnk_close',
                   'mslnk_read_attributes', 'mslnk_write_attributes',
                   'mslnk_read_link', 'mslnk_write_link']:
            setattr(ep, op, [Uint64Array_getitem(getattr(p, op), 0),
                             Uint64Array_getitem(getattr(p, op), 1)])

        return ep


class StoragedProfiler(object):
    def __init__(self):
        self.uptime = -1
        self.now = -1
        self.vers = '0.0.0'
        self.stat = []
        self.ports = []
        self.remove = []
        self.read = []
        self.write = []
        self.truncate = []
        self.io_process_ports = []


class StoragedRpcProxy(RpcProxy):
    def __init__(self, host, port=0):
        RpcProxy.__init__(self, host, port)

    def _connect(self):
        self._proxy = sp_client_t()
        self._proxy.host = self._host
        self._proxy.port = self._port
        sp_client_initialize(self._proxy)

    def _disconnect(self):
        sp_client_release(self._proxy)

    def get_profiler(self):
        p = spp_profiler_t()

        if sp_client_get_profiler(self._proxy, p) != 0:
            raise Exception("can't get storaged profiler.")

        sp = StoragedProfiler()
        sp.uptime = p.uptime
        sp.now = p.now
        sp.vers = p.vers

        sp.stat = [Uint64Array_getitem(p.stat, 0),
                   Uint64Array_getitem(p.stat, 1)]
        sp.ports = [Uint64Array_getitem(p.ports, 0),
                    Uint64Array_getitem(p.ports, 1)]
        sp.remove = [Uint64Array_getitem(p.remove, 0),
                    Uint64Array_getitem(p.remove, 1)]
        sp.io_process_ports = []
        for ports in range(0, p.nb_io_processes):
            sp.io_process_ports.append(Uint16Array_getitem(p.io_process_ports, ports))
        sp.read = [Uint64Array_getitem(p.read, 0),
                   Uint64Array_getitem(p.read, 1),
                   Uint64Array_getitem(p.read, 2)]
        sp.write = [Uint64Array_getitem(p.write, 0),
                    Uint64Array_getitem(p.write, 1),
                    Uint64Array_getitem(p.write, 2)]
        sp.truncate = [Uint64Array_getitem(p.truncate, 0),
                       Uint64Array_getitem(p.truncate, 1),
                       Uint64Array_getitem(p.truncate, 2)]

        return sp


class MountProfiler(object):
    def __init__(self):
        self.uptime = -1
        self.now = -1
        self.vers = '0.0.0'
        self.rozofs_ll_lookup = []
        self.rozofs_ll_forget = []
        self.rozofs_ll_getattr = []
        self.rozofs_ll_setattr = []
        self.rozofs_ll_readlink = []
        self.rozofs_ll_mknod = []
        self.rozofs_ll_mkdir = []
        self.rozofs_ll_unlink = []
        self.rozofs_ll_rmdir = []
        self.rozofs_ll_symlink = []
        self.rozofs_ll_rename = []
        self.rozofs_ll_open = []
        self.rozofs_ll_link = []
        self.rozofs_ll_read = []
        self.rozofs_ll_write = []
        self.rozofs_ll_flush = []
        self.rozofs_ll_release = []
        self.rozofs_ll_opendir = []
        self.rozofs_ll_readdir = []
        self.rozofs_ll_releasedir = []
        self.rozofs_ll_fsyncdir = []
        self.rozofs_ll_statfs = []
        self.rozofs_ll_setxattr = []
        self.rozofs_ll_getxattr = []
        self.rozofs_ll_listxattr = []
        self.rozofs_ll_removexattr = []
        self.rozofs_ll_access = []
        self.rozofs_ll_create = []
        self.rozofs_ll_getlk = []
        self.rozofs_ll_setlk = []
        self.rozofs_ll_ioctl = []


class MountRpcProxy(RpcProxy):
    def __init__(self, host, port=0):
        RpcProxy.__init__(self, host, port)

    def _connect(self):
        self._proxy = mp_client_t()
        self._proxy.host = self._host
        self._proxy.port = self._port
        mp_client_initialize(self._proxy)

    def _disconnect(self):
        mp_client_release(self._proxy)

    def get_profiler(self):
        p = mpp_profiler_t()
        if mp_client_get_profiler(self._proxy, p) != 0:
            raise Exception("can't get mount profiler.")

        mp = MountProfiler()
        mp.uptime = p.uptime
        mp.now = p.now
        mp.vers = p.vers

        mp.rozofs_ll_read = [Uint64Array_getitem(p.rozofs_ll_read, 0),
                               Uint64Array_getitem(p.rozofs_ll_read, 1),
                               Uint64Array_getitem(p.rozofs_ll_read, 2)]
        mp.rozofs_ll_write = [Uint64Array_getitem(p.rozofs_ll_write, 0),
                               Uint64Array_getitem(p.rozofs_ll_write, 1),
                               Uint64Array_getitem(p.rozofs_ll_write, 2)]
        for op in ["rozofs_ll_lookup", "rozofs_ll_forget", "rozofs_ll_getattr",
                   "rozofs_ll_setattr", "rozofs_ll_readlink", "rozofs_ll_mknod",
                   "rozofs_ll_mkdir", "rozofs_ll_unlink", "rozofs_ll_rmdir",
                   "rozofs_ll_symlink", "rozofs_ll_rename", "rozofs_ll_open",
                   "rozofs_ll_link", "rozofs_ll_flush", "rozofs_ll_release",
                   "rozofs_ll_opendir", "rozofs_ll_readdir", "rozofs_ll_releasedir",
                   "rozofs_ll_fsyncdir", "rozofs_ll_statfs", "rozofs_ll_setxattr",
                   "rozofs_ll_getxattr", "rozofs_ll_listxattr", "rozofs_ll_removexattr",
                   "rozofs_ll_access", "rozofs_ll_create", "rozofs_ll_getlk",
                   "rozofs_ll_setlk", "rozofs_ll_ioctl"]:
            setattr(mp, op, [Uint64Array_getitem(getattr(p, op), 0),
                             Uint64Array_getitem(getattr(p, op), 1)])

        return mp

class Node(object):

    def __init__(self, host, roles=0):
        self._host = host
        self._roles = roles
        self._platform_proxy = None
        # for all below key is Role
        self._proxies = {}
        self._rpcproxies = {}  # storaged and shares ares arrays dues to multi processes
        # does our node is connected and running
        self._up = False
        self._connected = False

    def __del(self):
        self._release_proxies()

    def _initialize_proxies(self):
        try:
            self._platform_proxy = get_proxy(self._host, PLATFORM_MANAGER)
            if self.has_roles(Role.EXPORTD) and Role.EXPORTD not in self._proxies:
                self._proxies[Role.EXPORTD] = get_proxy(self._host, EXPORTD_MANAGER)
            if self.has_roles(Role.STORAGED) and Role.STORAGED not in self._proxies:
                self._proxies[Role.STORAGED] = get_proxy(self._host, STORAGED_MANAGER)
            if self.has_roles(Role.SHARE) and Role.SHARE not in self._proxies:
                self._proxies[Role.SHARE] = get_proxy(self._host, SHARE_MANAGER)
        except Exception as e:
            self._release_proxies()
            raise e

    def _release_proxies(self):
        self._platform_proxy.getProxy()._release()
        for p in self._proxies.values():
            p.getProxy()._release()
        self._up = False

    def _connect_rpc(self):
        try:
            if self.has_roles(Role.EXPORTD) and Role.EXPORTD not in self._rpcproxies:
                if self._proxies[Role.EXPORTD].get_service_status() == ServiceStatus.STOPPED:
                    self._rpcproxies[Role.EXPORTD] = None
                else:
                    self._rpcproxies[Role.EXPORTD] = ExportdRpcProxy(self._host)
            if self.has_roles(Role.STORAGED) and Role.STORAGED not in self._rpcproxies:
                if self._proxies[Role.STORAGED].get_service_status() == ServiceStatus.STOPPED:
                    self._rpcproxies[Role.STORAGED] = None
                else:
                    self._rpcproxies[Role.STORAGED] = []
                    self._rpcproxies[Role.STORAGED].append(StoragedRpcProxy(self._host))
                    sp = self._rpcproxies[Role.STORAGED][0].get_profiler()
                    for port in sp.io_process_ports:
                        self._rpcproxies[Role.STORAGED].append(StoragedRpcProxy(self._host, port))
            if self.has_roles(Role.SHARE) and Role.SHARE not in self._rpcproxies:
                if self._proxies[Role.SHARE].get_service_status() == ServiceStatus.STOPPED:
                    self._rpcproxies[Role.SHARE] = None
                else:
                    self._rpcproxies[Role.SHARE] = []
                    sc = self._proxies[Role.SHARE].get_service_config()
                    for port in [s.profiling_port for s in sc.shares]:
                        self._rpcproxies[Role.SHARE].append(MountRpcProxy(self._host, port))
        except Exception as e:
            self._disconnect_rpc()
            raise e

    def _disconnect_rpc(self):
        for k in self._proxies.keys():
            del self._proxies[k]
        self._connected = False

    def _try_up(self):
        """ check if a node is up if no try to set it up """
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

    def _try_connect(self):
        """ check if a node has running rpc service and connect to it """
        if not self._try_up():
            return None

        if self._connected == False:
            # for role in [r for r in Role.ROLES if self.has_roles(r)]:
            #    if self._proxies[role].get_service_status() == ServiceStatus.STOPPED:
            #        self._proxies[role] = None
            try:
                self._connect_rpc()
            except:
                return False
            self._connected = True

        return True

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

    def check_platform_manager(self):
        if not self._try_up():
            return False

        return self._platform_proxy.ping()

    def get_platform_config(self):
        if not self._try_up():
            return None

        return self._platform_proxy.get_service_config()

    def set_platform_config(self, configuration):
        if not self._try_up():
            return

        self._platform_proxy.set_service_config(configuration)

    def get_configurations(self, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
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

    def get_statuses(self, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
        if not self._try_up():
            return None

        statuses = {}
        for role in [r for r in Role.ROLES if r & roles == r and self.has_roles(r)]:
            statuses[role] = self._proxies[role].get_service_status()

        return statuses

    def get_profilers(self, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
        if not self._try_connect():
            return None

        profilers = {}
        for role in [r for r in Role.ROLES if r & roles == r and self.has_roles(r)]:
            if role == Role.EXPORTD:
                if self._rpcproxies[role] is not None:
                    profilers[Role.EXPORTD] = self._rpcproxies[role].get_profiler()
                else:
                    profilers[Role.EXPORTD] = None
            else:
                if self._rpcproxies[role] is not None:
                    profilers[role] = []
                    for p in self._rpcproxies[role]:
                        profilers[role].append(p.get_profiler())
                else:
                    profilers[role] = None
        return profilers

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
        # self._hostname = hostname
        # self._proxy = get_proxy(hostname, PLATFORM_MANAGER)
        # self._config = self._proxy.get_service_config()
        self._nodes = self._get_nodes(hostname)

#    def __del__(self):
#        self._proxy.getProxy()._release()

    def _get_nodes(self, hostname):
        # get a proxy on the given platform host
        # should be used only once before
        # after setup we always used a node from nodes e.g. nodes[O]
        proxy = get_proxy(hostname, PLATFORM_MANAGER)
        config = proxy.get_service_config()
        nodes = {}

        # first set
        if not config.exportd_hostname:
            config.exportd_hostname = hostname

        exportd_node = Node(config.exportd_hostname, Role.EXPORTD)

        nodes[config.exportd_hostname] = exportd_node
        econfig = exportd_node.get_configurations(Role.EXPORTD)[Role.EXPORTD]

        if econfig is None:
            raise "Exportd node is off line."

        # if platform is sharing all storaged node share !!!
        roles = Role.STORAGED if not config.protocols else Role.STORAGED | Role.SHARE
        for h in [s for v in econfig.volumes.values()
                        for c in v.clusters.values()
                        for s in c.storages.values()]:
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


    def get_exportd_hostname(self):
        return self._nodes.values()[0].get_platform_config().exportd_hostname

    def set_exportd_hostname(self, hostname):
        # each node is a platform manager check if reachable
        # before make changes

        for n in self._nodes.values():
            n.check_platform_manager()

        # the appli change (best effort)
        for n in self._nodes.values():
            configuration = n.get_platform_config()
            configuration.exportd_hostname = hostname
            n.set_platform_config(configuration)


    def list_nodes(self, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
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

    def get_sharing_protocols(self):
        self._nodes.values()[0].get_platform_config().protocols

    def set_sharing_protocols(self, protocols=PROTOCOLS_VALUES):
        """ Set protocols used for sharing on this platform """
        for protocol in protocols:
            if protocol not in PROTOCOLS_VALUES:
                raise Exception("Unknown protocol: %s" % protocol)

        # check if all nodes are reachable
        for n in self._nodes.values():
            n.check_platform_manager()

        # get exportd hostname
        exportd_hostname = self.get_exportd_hostname()

        # build config to send to every sharing nodes.
        sconfig = ShareConfig(protocols)
        if protocols:
            enode = Node(exportd_hostname, Role.EXPORTD)
            econfig = enode.get_configurations(Role.EXPORTD)
            for export in econfig[Role.EXPORTD].exports.values():
                sconfig.shares.append(Share(exportd_hostname, export.root, 0))

        for n in self._nodes.values():
            configuration = n.get_platform_config()
            configuration.protocols = protocols
            n.set_platform_config(configuration)

            # every storaged become a share node
            if n.has_one_of_roles(Role.STORAGED):
                n.set_roles(n.get_roles() | Role.SHARE)
                n.set_configurations({Role.SHARE: sconfig})

    def get_statuses(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
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

    def set_status(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE, status=ServiceStatus.STOPPED):
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

    def start(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
        """ Convenient method to start all nodes with a role
        Args:
            the roles to be started
        """

        if hosts is None:
            hosts = self._nodes.keys()

        # take care of the starting order
        if roles & Role.STORAGED == Role.STORAGED:
            self.set_status(hosts, Role.STORAGED, ServiceStatus.STARTED)
        if roles & Role.EXPORTD == Role.EXPORTD:
            self.set_status(hosts, Role.EXPORTD, ServiceStatus.STARTED)
        if roles & Role.SHARE == Role.SHARE:
            time.sleep(1)
            self.set_status(hosts, Role.SHARE, ServiceStatus.STARTED)

    def stop(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
        """ Convenient method to stop all nodes with a role
        Args:
            the roles to be stopped
        """
        if hosts is None:
            hosts = self._nodes.keys()

        # take care of the stopping order
        if roles & Role.SHARE == Role.SHARE:
            self.set_status(hosts, Role.SHARE, ServiceStatus.STOPPED)
        if roles & Role.EXPORTD == Role.EXPORTD:
            self.set_status(hosts, Role.EXPORTD, ServiceStatus.STOPPED)
        if roles & Role.STORAGED == Role.STORAGED:
            self.set_status(hosts, Role.STORAGED, ServiceStatus.STOPPED)

    def get_profilers(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
        """ Get profilers for named nodes and roles

        Args:
            hosts : list of hosts to get profiles from, if None all host are
                    checked

            roles : for which roles statuses should be retrieved
                    if a given host doesn't have this role the return profiles
                    will not contain key for this role.

        Return:
            A dict: keys are host names, values are dicts {Role: profile}
                    or None if node is off line. profile arrays in case
                    of storaged or share roles
        """
        profilers = {}
        if hosts is None:
            for h, n in self._nodes.items():
                profilers[h] = n.get_profilers(roles)
        else:
            for h in hosts:
                profilers[h] = self._nodes[h].get_profilers(roles)

        return profilers

    def get_configurations(self, hosts=None, roles=Role.EXPORTD | Role.STORAGED | Role.SHARE):
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
            raise Exception("Invalid layout: %d" % layout)

        node = Node(self.get_exportd_hostname(), Role.EXPORTD)
        configuration = node.get_configurations(Role.EXPORTD)

        if configuration is None:
            raise "Exportd node is off line."

        if len(configuration[Role.EXPORTD].volumes) != 0:
            raise Exception("platform has configured volume(s) !!!")

        configuration[Role.EXPORTD].layout = layout
        node.set_configurations(configuration)

    def add_nodes(self, hosts, vid=None):
        """ Add storaged nodes to the platform

        Args:
            vid: volume to use, if none a new one will be created
            hosts: hosts to be added
        """
        exportd_hostname = self.get_exportd_hostname()
        sharing_protocols = self.get_sharing_protocols()

        enode = Node(exportd_hostname, Role.EXPORTD)
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
        roles = Role.STORAGED if not sharing_protocols else Role.STORAGED | Role.SHARE
        shconfig = ShareConfig(sharing_protocols)
        if sharing_protocols:
            for export in econfig[Role.EXPORTD].exports.values():
                shconfig.shares.append(Share(exportd_hostname, export.root, 0))

        for h in hosts:
            # if it's a new node register it (and duplicate platform config)
            # else just update its role
            if h not in self._nodes:
                pconfig = self._nodes.values()[0].get_platform_config()
                self._nodes[h] = Node(h, roles)
                self._nodes[h].set_platform_config(pconfig)
            else:
                # maybe overkill since the node could already have this role
                self._nodes[h].set_roles(self._nodes[h].get_roles() | roles)

            # configure the storaged
            sconfig = self._nodes[h].get_configurations(Role.STORAGED)
            # XXX root could (should ?) be compute on storaged module
            sconfig[Role.STORAGED].storages[(cid, sid)] = StorageConfig(cid, sid,
                                                                 "/srv/rozofs/storage_%d_%d" % (cid, sid))
            sconfig[Role.SHARE] = shconfig
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
        enode = Node(self.get_exportd_hostname(), Role.EXPORTD)
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

    def add_export(self, vid, name=None, passwd=None, squota="", hquota=""):
        """ Export a new file system

        Args:
            vid: the volume id to use the file system relies on
        """
        exportd_hostname = self.get_exportd_hostname()
        enode = Node(exportd_hostname, Role.EXPORTD)
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
            if "/srv/rozofs/%s" % name in [e.root for e in econfig[Role.EXPORTD].exports.values()]:
                raise Exception("Duplicate export name: %s" % name)

        # compute md5
        if passwd is None:
            md5 = ""
        else:
            with open('/dev/null', 'w') as devnull:
                md5 = subprocess.check_output(['md5pass', passwd, 'rozofs'],
                                   stderr=devnull)[11:].rstrip()

        # add this new export
        econfig[Role.EXPORTD].exports[eid] = ExportConfig(eid, vid, "/srv/rozofs/%s" % name, md5, squota, hquota)
        enode.set_configurations(econfig)

        # if sharing is enable share this new export
        for n in self._nodes.values():
            # every storaged become a share node
            if n.has_one_of_roles(Role.SHARE):
                sconfig = n.get_configurations(Role.SHARE)
                sconfig[Role.SHARE].shares.append(Share(exportd_hostname ,
                                                        "/srv/rozofs/%s" % name, -1))
                n.set_configurations(sconfig)

    def update_export(self, eid, passwd=None, squota=None, hquota=None):
        """ Modify an exported file system

        Args:
            eid: the export id to modify
            passwd: password to set if None no modification is done
            squota: soft quota to set if None no modification is done
            hquota: hard quota to set if None no modification is done
        """
        enode = Node(self.get_exportd_hostname(), Role.EXPORTD)
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
            if passwd:
                with open('/dev/null', 'w') as devnull:
                    econfig[Role.EXPORTD].exports[eid].md5 = subprocess.check_output(['md5pass', passwd, 'rozofs'],
                                   stderr=devnull)[11:].rstrip()
            else:
                econfig[Role.EXPORTD].exports[eid].md5 = ""

        # update this export
        enode.set_configurations(econfig)

    def remove_export(self, eid, force=False):
        """ Remove an exported file system

        To not keep unused data stored, exports containing files will not be remove
        unless force is set to True.

        Args:
            eid: the export id to remove
            force: force removing of non empty exportd
        """
        exportd_hostname = self.get_exportd_hostname()
        enode = Node(exportd_hostname, Role.EXPORTD)
        econfig = enode.get_configurations(Role.EXPORTD)
        eprofiler = enode.get_profilers(Role.EXPORTD)

        if econfig is None or eprofiler is None:
            raise Exception("Exportd node is off line.")

        if eid not in econfig[Role.EXPORTD].exports.keys():
            raise Exception("Unknown export: %d." % eid)

        for estat in eprofiler[Role.EXPORTD].estats:
            if estat.eid == eid and estat.files != 0 and not force:
                raise Exception("Can't remove non empty export (use  force=True)")

        expconfig = econfig[Role.EXPORTD].exports.pop(eid)
        # if sharing is enable unshare this export
        share = Share(exportd_hostname , expconfig.root, -1)
        for n in self._nodes.values():
            if n.has_one_of_roles(Role.SHARE):
                sconfig = n.get_configurations(Role.SHARE)
                sconfig[Role.SHARE].shares.remove(share)
                n.set_configurations(sconfig)

        enode.set_configurations(econfig)

