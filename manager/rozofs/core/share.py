# -*- coding: utf-8 -*-

import os
import subprocess
from rozofs.core.agent import Agent, ServiceStatus
from rozofs.core.constants import SHARE_MANAGER, PROTOCOLS
import shutil
from rozofs.core.configuration import ConfigurationParser, ConfigurationReader, \
    ConfigurationWriter
from rozofs.core.libconfig import config_setting_add, CONFIG_TYPE_LIST, \
    CONFIG_TYPE_STRING, config_setting_set_string, config_lookup, \
    config_setting_length, config_setting_get_elem, config_setting_get_string
from rozofs.ext.fstab import Fstab, Line

class Share(object):
    def __init__(self, export_host, export_path, profiling_port):
        self.export_host = export_host
        self.export_path = export_path
        self.profiling_port = profiling_port

    def __eq__(self, other):
        return self.export_host == other.export_host and self.export_path == other.export_path

class ShareConfig(object):
    def __init__(self, protocols=[], shares=[]):
        self.protocols = protocols
        self.shares = shares

class PlatformConfigurationParser(ConfigurationParser):

    def parse(self, configuration, config):
        protocol_settings = config_setting_add(config.root, PROTOCOLS, CONFIG_TYPE_LIST)
        for protocol in configuration.protocols:
            protocol_setting = config_setting_add(protocol_settings, '', CONFIG_TYPE_STRING)
            config_setting_set_string(protocol_setting, protocol)

    def unparse(self, config, configuration):
        protocol_settings = config_lookup(config, PROTOCOLS)

        configuration.protocols = []
        if protocol_settings is not None:
            for i in range(config_setting_length(protocol_settings)):
                protocol_setting = config_setting_get_elem(protocol_settings, i)
                configuration.protocols.append(config_setting_get_string(protocol_setting))

class ShareAgent(Agent):
    """
    Manage shares
    All mount rozofs are shared across protocols (only nfs for now)
    """

    __FSTAB = '/etc/fstab'
    __MTAB = '/etc/mtab'
    __EXPORTS = '/etc/exports'
    __ETAB = '/var/lib/nfs/etab'

    def __init__(self, mountdir='/mnt', config='/etc/rozofs/sharing.conf'):
        """
        ctor
        @param mountdir : the parent directory name for rozofs mount points
        """
        Agent.__init__(self, SHARE_MANAGER)
        self._mountdir = mountdir
        self._reader = ConfigurationReader(config, PlatformConfigurationParser())
        self._writer = ConfigurationWriter(config, PlatformConfigurationParser())

    def _share_path(self, share):
        return os.path.join(self._mountdir, "rozofs@%s" %
                            share.export_host,
                             share.export_path.split('/')[-1])

    #
    # mount points management
    #
    def _mount(self, share):
        cmds = ['mount', self._share_path(share)]
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

    def _umount(self, share):
        cmds = ['umount', self._share_path(share)]
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

    def _list_mount(self):
        with open(self.__MTAB, 'r') as mtab:
            mount = [l.split()[1] for l in mtab if l.split()[0] == "rozofs"]
        return mount

    def _is_mount(self, share):
        return self._share_path(share) in self._list_mount()

    def _add_share_mountpoint(self, share):
        fstab = Fstab()
        fstab.read(self.__FSTAB)
        mount_path = self._share_path(share)
        if not os.path.exists(mount_path):
            os.makedirs(mount_path)
        # add a line to fstab
        fstab.lines.append(Line("rozofsmount\t%s\trozofs\texporthost=%s,exportpath=%s,_netdev\t0\t0\n" % (mount_path, share.export_host, share.export_path)))
        fstab.write(self.__FSTAB)

    def _remove_share_mountpoint(self, share):
        fstab = Fstab()
        fstab.read(self.__FSTAB)
        mount_path = self._share_path(share)
        if os.path.exists(mount_path):
            os.rmdir(mount_path)
        # remove the line from fstab
        newlines = [l for l in fstab.lines if l.directory != mount_path]
        fstab.lines = newlines
        fstab.write(self.__FSTAB)

    # return [(share, mountpoint)]
#    def _list_share_mountpoints(self):
#        fstab = Fstab()
#        fstab.read(self.__FSTAB)
#        mpts = []
#        for l in fstab.get_rozofs_lines():
#            o = l.get_rozofs_options()
#            #with open("/var/run/rozofsmount%s" (l.directory.replace('/', '.'))) as f:
#            mpts.append((Share(o["host"], o["path"], f.read()), l.directory))
#        return shares

#        return [(Share(o["host"], o["path"]), l.directory)
#                for l in fstab.get_rozofs_lines()
#                for o in l.get_rozofs_options()]


    #
    # nfs export management
    #
    def _nfs_share(self):
        cmds = ['exportfs', '-r']
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

    def _list_nfs_shared(self):
        with open(self.__ETAB, 'r') as etab:
            shares = [l.split()[0] for l in etab if l.startswith('%s/rozofs@' % self._mountdir)]
        return shares

    def _add_nfs_share(self, share):
        with open(self.__EXPORTS, 'a') as exports:
            exports.write('%s *(rw,all_squash,fsid=10,no_subtree_check,anonuid=1000,anongid=1000,sync)\n' % self._share_path(share))

    def _remove_nfs_share(self, share):
        path = self._share_path(share)
        with open("/tmp/exports", 'w') as tmp:
            with open(self.__EXPORTS, 'r') as exports:
                tmp.writelines([l for l in exports.readlines() if not l.startswith(path)])
        shutil.copy("/tmp/exports", self.__EXPORTS)

    # return the share name
    def _list_share_nfs(self):
        with open(self.__EXPORTS, 'r') as exports:
            shares = [l.split()[0] for l in exports.readlines() if l.strip().startswith('%s/rozofs@' % self._mountdir)]
        return shares

    #
    # shares management
    #
    def _list_shares(self):
        fstab = Fstab()
        fstab.read(self.__FSTAB)
        shares = []
        for l in fstab.get_rozofs_lines():
            o = l.get_rozofs_options()
            try:
                f = open("/var/run/rozofsmount%s" % (l.directory.replace('/', '.')))
                port = int(f.read())
                f.close()
            except:
                port = -1  # if not mount port is set to -1
            shares.append(Share(o["host"], o["path"], port))

        return shares

    def _add_share(self, share):
        self._add_share_mountpoint(share)
        self._mount(share)
        self._add_nfs_share(share)
        self._nfs_share()
        # TODO smb, afp

    def _remove_share(self, share):
        self._remove_nfs_share(share)
        self._nfs_share()
        # TODO smb, afp
        if self._is_mount(share):
            self._umount(share)
        self._remove_share_mountpoint(share)

    def _share(self):
        cmds = ['mount', '-a', '-t', 'rozofs']
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

        cmds = ['exportfs', "-a"]
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

    def _unshare(self):
        cmds = ['exportfs', "-au"]
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

        # umount -at doesn't work with unprivileged mount
#        cmds = ['umount', '-a', '-t', 'rozofs']
#        with open('/dev/null', 'w') as devnull:
#            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
#            if p.wait() is not 0 :
#                raise Exception(p.communicate()[1])
        for share in self._list_shares():
            self._umount(share)

    def get_service_config(self):
        configuration = ShareConfig()
        self._reader.read(configuration)
        configuration.shares = self._list_shares()
        return configuration

    def set_service_config(self, configuration):
        self._writer.write(configuration)
        shares = self._list_shares()
        for share in [s for s in configuration.shares if s not in shares]:
            self._add_share(share)
        for share in [s for s in shares if s not in configuration.shares]:
            self._remove_share(share)

    # return True for each share if mounted and shared (nfs only for now)
    def get_service_status(self):
        shares = [self._share_path(s) for s in self._list_shares()]
        mounts = self._list_mount()
        shared = self._list_nfs_shared()
        return not False in [s in mounts and s in shared for s in shares]

    def set_service_status(self, status):
        if status == ServiceStatus.STARTED:
            self._share()
        if status == ServiceStatus.STOPPED:
            self._unshare()
