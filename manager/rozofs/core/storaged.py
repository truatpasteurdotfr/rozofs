# -*- coding: utf-8 -*-

import os
from rozofs.core.configuration import ConfigurationParser, ConfigurationReader, \
    ConfigurationWriter
from rozofs.core.libconfig import config_setting_add, CONFIG_TYPE_INT, \
    config_setting_set_int, CONFIG_TYPE_LIST, CONFIG_TYPE_GROUP, CONFIG_TYPE_STRING, \
    config_setting_set_string, config_lookup, config_setting_get_int, \
    config_setting_length, config_setting_get_elem, config_setting_get_member, \
    config_setting_get_string, config_setting_set_int_elem, \
    config_setting_get_int_elem
from rozofs.core.daemon import DaemonManager
from rozofs.core.constants import LAYOUT, STORAGES, STORAGE_SID, STORAGE_CID, STORAGE_ROOT, \
    LAYOUT_2_3_4, STORAGED_MANAGER, LAYOUT_4_6_8, LAYOUT_8_12_16, PORTS
from rozofs.core.agent import Agent, ServiceStatus
import collections


class StorageConfig():
    def __init__(self, cid=0, sid=0, root=""):
        self.cid = cid
        self.sid = sid
        self.root = root


class StoragedConfig():
    def __init__(self, ports=[], storages={}):
        # keys is a tuple (cid, sid)
        self.storages = storages
        self.ports = ports

#    def check_consistenty(self):
#        if self.layout not in [LAYOUT_2_3_4, LAYOUT_4_6_8, LAYOUT_8_12_16]:
#            raise Exception("invalid layout %d" % self.layout)
#
#        # check sid uniqueness
#        y = collections.Counter([s.vid for s in self.storages])
#        d = [i for i in y if y[i] > 1]
#        if d:
#            raise Exception("duplicated sid(s): [%s]" % ', '.join(map(str, d)))


class StoragedConfigurationParser(ConfigurationParser):

    def parse(self, configuration, config):
#        layout_setting = config_setting_add(config.root, LAYOUT, CONFIG_TYPE_INT)
#        config_setting_set_int(layout_setting, configuration.layout)

        ports_setting = config_setting_add(config.root, PORTS, CONFIG_TYPE_LIST)
        for port in configuration.ports:
            config_setting_set_int_elem(ports_setting, -1, port)

        storage_settings = config_setting_add(config.root, STORAGES, CONFIG_TYPE_LIST)
        for storage in configuration.storages.values():
            storage_setting = config_setting_add(storage_settings, '', CONFIG_TYPE_GROUP)
            cid_setting = config_setting_add(storage_setting, STORAGE_CID, CONFIG_TYPE_INT)
            config_setting_set_int(cid_setting, storage.cid)
            sid_setting = config_setting_add(storage_setting, STORAGE_SID, CONFIG_TYPE_INT)
            config_setting_set_int(sid_setting, storage.sid)
            root_setting = config_setting_add(storage_setting, STORAGE_ROOT, CONFIG_TYPE_STRING)
            config_setting_set_string(root_setting, storage.root)

    def unparse(self, config, configuration):
#        layout_setting = config_lookup(config, LAYOUT)
#        if (layout_setting == None):
#            raise Exception("Wrong format: no layout defined.")
#
#        configuration.layout = config_setting_get_int(layout_setting)

        port_settings = config_lookup(config, PORTS)
        configuration.ports = []
        for i in range(config_setting_length(port_settings)):
            configuration.ports.append(config_setting_get_int_elem(port_settings, i))

        storage_settings = config_lookup(config, STORAGES)
        # if (storage_settings == None):
        #    raise Exception(get_config.error_text)
        configuration.storages = {}
        for i in range(config_setting_length(storage_settings)):
            storage_setting = config_setting_get_elem(storage_settings, i)
            cid_setting = config_setting_get_member(storage_setting, STORAGE_CID)
            cid = config_setting_get_int(cid_setting)
            sid_setting = config_setting_get_member(storage_setting, STORAGE_SID)
            sid = config_setting_get_int(sid_setting)
            root_setting = config_setting_get_member(storage_setting, STORAGE_ROOT)
            root = config_setting_get_string(root_setting)
            configuration.storages[(cid, sid)] = StorageConfig(cid, sid, root)


class StoragedAgent(Agent):

    def __init__(self, config='/etc/rozofs/storage.conf', daemon='storaged'):
        Agent.__init__(self, STORAGED_MANAGER)
        self._daemon_manager = DaemonManager(daemon, ["-c", config], 1)
        self._reader = ConfigurationReader(config, StoragedConfigurationParser())
        self._writer = ConfigurationWriter(config, StoragedConfigurationParser())

    def get_service_config(self):
        configuration = StoragedConfig()
        return self._reader.read(configuration)

    def set_service_config(self, configuration):
        for r in [s.root for s in configuration.storages.values()]:
            if not os.path.isabs(r):
                raise Exception('%s: not absolute.' % r)
            if not os.path.exists(r):
                os.mkdir(r)
            if not os.path.isdir(r):
                raise Exception('%s: not a directory.' % r)

        self._writer.write(configuration)
        self._daemon_manager.restart()

    def get_service_status(self):
        return self._daemon_manager.status()

    def set_service_status(self, status):
        current_status = self._daemon_manager.status()
        if status == ServiceStatus.STARTED and not current_status:
            self._daemon_manager.start()
        if status == ServiceStatus.STOPPED and current_status:
            self._daemon_manager.stop()
