# -*- coding: utf-8 -*-

from rozofs.core.configuration import ConfigurationParser, ConfigurationReader, \
    ConfigurationWriter
from rozofs.core.libconfig import config_setting_add, CONFIG_TYPE_INT, \
    config_setting_set_int, CONFIG_TYPE_LIST, CONFIG_TYPE_GROUP, CONFIG_TYPE_STRING, \
    config_setting_set_string, config_lookup, config_setting_get_int, \
    config_setting_length, config_setting_get_elem, config_setting_get_member, \
    config_setting_get_string
from rozofs.core.constants import LAYOUT, VOLUME, VOLUME_VID, VOLUME_CID, \
    VOLUME_SIDS, VOLUME_SID, VOLUME_HOST, EXPORTS, EXPORT_EID, EXPORT_ROOT, \
    EXPORT_MD5, EXPORT_SQUOTA, EXPORT_HQUOTA, VOLUME_CIDS, LAYOUT_2_3_4, VOLUMES, \
    EXPORTD_MANAGER, LAYOUT_4_6_8, LAYOUT_8_12_16
from rozofs.core.daemon import DaemonManager
from rozofs import __sysconfdir__
import os
from rozofs.core.agent import Agent, ServiceStatus
import subprocess
import shutil

class StorageStat():
    def __init__(self, host="", size=0, free=0):
        self.host = host
        self.size = size
        self.free = free

class ClusterStat():
    def __init__(self, size=0, free=0, sstats=None):
        self.size = size
        self.free = free
        if sstats is None:
            self.sstats = {}
        else:
            self.sstats = sstats

class VolumeStat():
    def __init__(self, bsize=0, bfree=0, blocks=0, cstats=None):
        self.bsize = bsize
        self.bfree = bfree
        self.blocks = blocks
        if cstats is None:
            self.cstats = {}
        else:
            self.cstats = cstats

class ExportdStat():
    def __init__(self, vstats=None, estats=None):
        if vstats is None:
            self.vstats = {}
        else:
            self.vstats = vstats
        if estats is None:
            self.estats = {}
        else:
            self.estats = estats

class ClusterConfig():
    def __init__(self, cid, storages=None):
        self.cid = cid
        if storages is None:
            self.storages = {}
        else:
            self.storages = storages

    def __eq__(self, other):
        return self.__dict__ == other.__dict__


class VolumeConfig():
    def __init__(self, vid, clusters=None):
        self.vid = vid
        if clusters is None:
            self.clusters = {}
        else:
            self.clusters = clusters

    def __eq__(self, other):
        return self.__dict__ == other.__dict__


class ExportConfig():
    def __init__(self, eid, vid, root, md5, squota, hquota):
        self.eid = eid
        self.vid = vid
        self.root = root
        self.md5 = md5
        self.squota = squota
        self.hquota = hquota

    def __eq__(self, other):
        return self.__dict__ == other.__dict__


class ExportdConfig():
    def __init__(self, layout=LAYOUT_2_3_4, volumes=None, exports=None, stats=None):
        self.layout = layout
        if volumes is None:
            self.volumes = {}
        else:
            self.volumes = volumes
        if exports is None:
            self.exports = {}
        else:
            self.exports = exports
        if stats is None:
            self.stats = ExportdStat()
        else:
            self.stats = stats

    def __eq__(self, other):
        return self.__dict__ == other.__dict__


class ExportdConfigurationParser(ConfigurationParser):

    def parse(self, configuration, config):
        layout_setting = config_setting_add(config.root, LAYOUT, CONFIG_TYPE_INT)
        config_setting_set_int(layout_setting, int(configuration.layout))

        volumes_settings = config_setting_add(config.root, VOLUMES, CONFIG_TYPE_LIST)
        for volume in configuration.volumes.values():
            volume_settings = config_setting_add(volumes_settings, VOLUME, CONFIG_TYPE_GROUP)
            vid_setting = config_setting_add(volume_settings, VOLUME_VID, CONFIG_TYPE_INT)
            config_setting_set_int(vid_setting, int(volume.vid))
            clusters_settings = config_setting_add(volume_settings, VOLUME_CIDS, CONFIG_TYPE_LIST)
            for cluster in volume.clusters.values():
                cluster_setting = config_setting_add(clusters_settings, '', CONFIG_TYPE_GROUP)
                cid_setting = config_setting_add(cluster_setting, VOLUME_CID, CONFIG_TYPE_INT)
                config_setting_set_int(cid_setting, int(cluster.cid))
                sids_setting = config_setting_add(cluster_setting, VOLUME_SIDS, CONFIG_TYPE_LIST)
                for sid, host in cluster.storages.items():
                    storage_setting = config_setting_add(sids_setting, '', CONFIG_TYPE_GROUP)
                    sid_setting = config_setting_add(storage_setting, VOLUME_SID, CONFIG_TYPE_INT)
                    config_setting_set_int(sid_setting, int(sid))
                    host_setting = config_setting_add(storage_setting, VOLUME_HOST, CONFIG_TYPE_STRING)
                    config_setting_set_string(host_setting, str(host))

        export_settings = config_setting_add(config.root, EXPORTS, CONFIG_TYPE_LIST)
        for export in configuration.exports.values():
            export_setting = config_setting_add(export_settings, '', CONFIG_TYPE_GROUP)
            eid_setting = config_setting_add(export_setting, EXPORT_EID, CONFIG_TYPE_INT)
            config_setting_set_int(eid_setting, int(export.eid))
            vid_setting = config_setting_add(export_setting, VOLUME_VID, CONFIG_TYPE_INT)
            config_setting_set_int(vid_setting, int(export.vid))
            root_setting = config_setting_add(export_setting, EXPORT_ROOT, CONFIG_TYPE_STRING)
            config_setting_set_string(root_setting, str(export.root))
            md5_setting = config_setting_add(export_setting, EXPORT_MD5, CONFIG_TYPE_STRING)
            config_setting_set_string(md5_setting, str(export.md5))
            sqt_setting = config_setting_add(export_setting, EXPORT_SQUOTA, CONFIG_TYPE_STRING)
            config_setting_set_string(sqt_setting, str(export.squota))
            hqt_setting = config_setting_add(export_setting, EXPORT_HQUOTA, CONFIG_TYPE_STRING)
            config_setting_set_string(hqt_setting, str(export.hquota))

    def unparse(self, config, configuration):
        layout_setting = config_lookup(config, LAYOUT)
        if layout_setting == None:
            raise Exception("wrong format: no layout defined.")

        configuration.layout = config_setting_get_int(layout_setting)

        volumes_setting = config_lookup(config, VOLUMES)
        # if volumes_setting == None:
        #    raise Exception(config.error_text)

        configuration.volumes = {}
        if volumes_setting is not None:
            for i in range(config_setting_length(volumes_setting)):
                volume_setting = config_setting_get_elem(volumes_setting, i)
                vid_setting = config_setting_get_member(volume_setting, VOLUME_VID)
                vid = config_setting_get_int(vid_setting)
                clusters_setting = config_setting_get_member(volume_setting, VOLUME_CIDS)
                clusters = {}
                for j in range(config_setting_length(clusters_setting)):
                    cluster_setting = config_setting_get_elem(clusters_setting, j)
                    cid_setting = config_setting_get_member(cluster_setting, VOLUME_CID)
                    cid = config_setting_get_int(cid_setting)
                    storages = {}
                    sids_setting = config_setting_get_member(cluster_setting, VOLUME_SIDS)
                    for k in range(config_setting_length(sids_setting)):
                        storage_setting = config_setting_get_elem(sids_setting, k)
                        sid_setting = config_setting_get_member(storage_setting, VOLUME_SID)
                        sid = config_setting_get_int(sid_setting)
                        host_setting = config_setting_get_member(storage_setting, VOLUME_HOST)
                        host = config_setting_get_string(host_setting)
                        storages[sid] = host
                        clusters[cid] = ClusterConfig(cid, storages)
                    configuration.volumes[vid] = VolumeConfig(vid, clusters)

        export_settings = config_lookup(config, EXPORTS)
        # if export_settings == None:
        #    raise Exception(config.error_text)

        configuration.exports = {}
        if export_settings is not None:
            for i in range(config_setting_length(export_settings)):
                export_setting = config_setting_get_elem(export_settings, i)
                eid_setting = config_setting_get_member(export_setting, EXPORT_EID)
                eid = config_setting_get_int(eid_setting)
                vid_setting = config_setting_get_member(export_setting, VOLUME_VID)
                vid = config_setting_get_int(vid_setting)
                root_setting = config_setting_get_member(export_setting, EXPORT_ROOT)
                root = config_setting_get_string(root_setting)
                md5_setting = config_setting_get_member(export_setting, EXPORT_MD5)
                md5 = config_setting_get_string(md5_setting)
                sqt_setting = config_setting_get_member(export_setting, EXPORT_SQUOTA)
                sqt = config_setting_get_string(sqt_setting)
                hqt_setting = config_setting_get_member(export_setting, EXPORT_HQUOTA)
                hqt = config_setting_get_string(hqt_setting)
                configuration.exports[eid] = ExportConfig(eid, vid, root, md5, sqt, hqt)

class ExportdAgent(Agent):
    """ exportd agent """

    def __init__(self, config="%s%s" % (__sysconfdir__, '/rozofs/export.conf'), daemon='exportd'):
        Agent.__init__(self, EXPORTD_MANAGER)
        self._daemon_manager = DaemonManager(daemon, ["-c", config])
        self._reader = ConfigurationReader(config, ExportdConfigurationParser())
        self._writer = ConfigurationWriter(config, ExportdConfigurationParser())

    def _get_volume_stat(self, vid, vstat):
        # vstat = VolumeStat()
        # Did I ever understood python?
        # I can't figure out why the above line does not initialize
        # a new cstats (on second call)
        # vstat.cstats.clear()
        with open('/var/run/exportd/volume_%d' % vid, 'r') as input:
            cid = 0
            sid = 0
            for line in input:

                if line.startswith("bsize"):
                    vstat.bsize = int(line.split(':')[-1])
                if line.startswith("bfree"):
                    vstat.bfree = int(line.split(':')[-1])
                if line.startswith("blocks"):
                    vstat.blocks = int(line.split(':')[-1])
                if line.startswith("cluster"):
                    cid = int(line.split(':')[-1])
                    vstat.cstats[cid] = ClusterStat()
                    # vstat.cstats[cid].sstats.clear()
                if line.startswith("storage"):
                    sid = int(line.split(':')[-1])
                    vstat.cstats[cid].sstats[sid] = StorageStat()
                if line.startswith("host"):
                    vstat.cstats[cid].sstats[sid].host = line.split(':')[-1].rstrip()
                if line.startswith("size"):
                    if len(vstat.cstats[cid].sstats.keys()) == 0:
                        vstat.cstats[cid].size = int(line.split(':')[-1])
                    else:
                        vstat.cstats[cid].sstats[sid].size = int(line.split(':')[-1])
                if line.startswith("free"):
                    if len(vstat.cstats[cid].sstats.keys()) == 0:
                        vstat.cstats[cid].free = int(line.split(':')[-1])
                    else:
                        vstat.cstats[cid].sstats[sid].free = int(line.split(':')[-1])
        # return vstat

    def get_service_config(self):
        configuration = ExportdConfig()
        self._reader.read(configuration)
        for vid in configuration.volumes.keys():
            configuration.stats.vstats[vid] = VolumeStat()
            self._get_volume_stat(vid, configuration.stats.vstats[vid])
            print configuration.stats.vstats[vid].cstats
        print configuration.stats.vstats
        return configuration

    def set_service_config(self, configuration):
        current = ExportdConfig()
        self._reader.read(current)

        current_roots = [e.root for e in current.exports.values()]
        roots = [e.root for e in configuration.exports.values()]

        # create new ones
        for r in [r for r in roots if r not in current_roots]:
            if not os.path.isabs(r):
                raise Exception('%s: not absolute.' % r)

            if not os.path.exists(r):
                os.makedirs(r)

            if not os.path.isdir(r):
                raise Exception('%s: not a directory.' % r)

        self._writer.write(configuration)
        self._daemon_manager.reload()

        # delete no more used ones
        for r in [r for r in current_roots if r not in roots]:
            if os.path.exists(r):
                shutil.rmtree(r)

    def get_service_status(self):
        return self._daemon_manager.status()

    def set_service_status(self, status):
        current_status = self._daemon_manager.status()
        if status == ServiceStatus.STARTED and not current_status:
            self._daemon_manager.start()
        if status == ServiceStatus.STOPPED and current_status:
            self._daemon_manager.stop()


class ExportdPacemakerAgent(ExportdAgent):
    """ exportd managed thru pacemaker """

    def __init__(self, config='/etc/rozofs/export.conf', daemon='/usr/bin/exportd', resource='exportd_rozofs'):
        ExportdAgent.__init__(self, config, daemon)
        self._resource = resource

    def _start(self):
        cmds = ['crm', 'resource', 'start', self._resource]
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull,
                stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

    def _stop(self):
        cmds = ['crm', 'resource', 'stop', self._resource]
        with open('/dev/null', 'w') as devnull:
            p = subprocess.Popen(cmds, stdout=devnull,
                stderr=subprocess.PIPE)
            if p.wait() is not 0 :
                raise Exception(p.communicate()[1])

    def get_service_status(self):
        return self._daemon_manager.status()

    def set_service_status(self, status):
        current_status = self._daemon_manager.status()
        if status == ServiceStatus.STARTED and not current_status:
            self._start()
        if status == ServiceStatus.STOPPED and current_status:
            self._stop()

