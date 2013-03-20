from rozofs.core.agent import Agent, ServiceStatus
from rozofs.core.constants import ROZOFSMOUNT_MANAGER
import os
import subprocess
from rozofs.ext.fstab import Fstab, Line

class RozofsMountConfig(object):
    def __init__(self, export_host, export_path, profiling_port):
        self.export_host = export_host
        self.export_path = export_path
        self.profiling_port = profiling_port

    def __eq__(self, other):
        return self.export_host == other.export_host and self.export_path == other.export_path

class RozofsMountAgent(Agent):
    '''
    Managed rozofsmount via fstab
    '''

    __FSTAB = '/etc/fstab'
    __MTAB = '/etc/mtab'
    __FSTAB_LINE = "rozofsmount\t%s\trozofs\texporthost=%s,exportpath=%s,_netdev\t0\t0\n"

    def __init__(self, mountdir='/mnt'):
        """
        ctor
        @param mountdir : the parent directory name for rozofs mount points
        """
        Agent.__init__(self, ROZOFSMOUNT_MANAGER)
        self._mountdir = mountdir

    def _mount_path(self, export_host, export_path):
        return os.path.join(self._mountdir, "rozofs@%s" % export_host,
                            export_path.split('/')[-1])

    #
    # mount points management
    #
    def _mount(self, path):
        cmds = ['mount', path]
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

    def _umount(self, path):
        cmds = ['umount', path]
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

    def _list_mount(self):
        with open(self.__MTAB, 'r') as mtab:
            mount = [l.split()[1] for l in mtab if l.split()[0] == "rozofs"]
        return mount

#    def _is_mount(self, share):
#        return self._mount_path(share) in self._list_mount()

    def _add_mountpoint(self, export_host, export_path):
        fstab = Fstab()
        fstab.read(self.__FSTAB)
        mount_path = self._mount_path(export_host, export_path)
        if not os.path.exists(mount_path):
            os.makedirs(mount_path)
        # add a line to fstab
        fstab.lines.append(Line(self.__FSTAB_LINE % (mount_path, export_host, export_path)))
        fstab.write(self.__FSTAB)

    def _remove_mountpoint(self, export_host, export_path):
        fstab = Fstab()
        fstab.read(self.__FSTAB)
        mount_path = self._mount_path(export_host, export_path)
        if os.path.exists(mount_path):
            os.rmdir(mount_path)
        # remove the line from fstab
        newlines = [l for l in fstab.lines if l.directory != mount_path]
        fstab.lines = newlines
        fstab.write(self.__FSTAB)

    def _list_mountpoint(self):
        fstab = Fstab()
        fstab.read(self.__FSTAB)
        return [l.directory for l in fstab.get_rozofs_lines()]

    def get_service_config(self):
        fstab = Fstab()
        fstab.read(self.__FSTAB)
        configurations = []
        for l in fstab.get_rozofs_lines():
            o = l.get_rozofs_options()
            try:
                f = open("/var/run/rozofsmount%s" % (l.directory.replace('/', '.')))
                port = int(f.read())
                f.close()
            except:
                port = -1  # if not mount port is set to -1
            configurations.append(RozofsMountConfig(o["host"], o["path"], port))
        return configurations

    def set_service_config(self, configurations):
        currents = self.get_service_config()
        for config in [c for c in configurations if c not in currents]:
            self._add_mountpoint(config.export_host, config.export_path)
            self._mount(self._mount_path(config.export_host, config.export_path))
        for config in [c for c in currents if c not in configurations]:
            self._umount(self._mount_path(config.export_host, config.export_path))
            self._remove_mountpoint(config.export_host, config.export_path)

    def get_service_status(self):
        return self._list_mountpoint() == self._list_mount()

    def set_service_status(self, status):
        if status == ServiceStatus.STARTED:
            cmds = ['mount', '-a', '-t', 'rozofs']
            with open('/dev/null', 'w') as devnull:
                p = subprocess.Popen(cmds, stdout=devnull, stderr=subprocess.PIPE)
                if p.wait() is not 0 :
                    raise Exception(p.communicate()[1])

        if status == ServiceStatus.STOPPED:
            for mp in self._list_mount():
                self._umount(mp)
