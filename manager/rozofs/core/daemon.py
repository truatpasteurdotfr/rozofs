# -*- coding: utf-8 -*-

import subprocess
import os
import time

class DaemonManager(object):
    """ Manage rozofs daemons """
    def __init__(self, daemon, args=[], wait=0):
        '''
        Constructor
        @param daemon: the daemon
        '''
        # if not os.path.exists(daemon) :
        #    raise Exception(daemon + ": no such daemon.")
        self._daemon = daemon
        self._args = args
        self._wait = wait

    def status(self):
        '''
        Get status of the underlying daemon
        @return: True if running False otherwise
        '''
        r = True
        with open('/dev/null', 'w') as devnull:
            if subprocess.call(['pidof', self._daemon], stdout=devnull,
                    stderr=devnull) is not 0:
                r = False
        return r

    def start(self):
        '''
        Start the underlying daemon
        '''
        cmds = [self._daemon] + self._args
        if self.status() is False :
            with open('/dev/null', 'w') as devnull:
                p = subprocess.Popen(cmds, stdout=devnull,
                    stderr=subprocess.PIPE)
                if p.wait() is not 0 :
                    raise Exception(p.communicate()[1])

    def stop(self):
        '''
        Stop the underlying daemon
        '''
        cmds = ['killall', '-TERM', self._daemon]
        if self.status() is True :
            with open('/dev/null', 'w') as devnull:
                p = subprocess.Popen(cmds, stdout=devnull,
                    stderr=subprocess.PIPE)
                if p.wait() is not 0 :
                    raise Exception(p.communicate()[1])

    def restart(self):
        '''
        Restart the underlying daemon
        '''
        self.stop()
        time.sleep(self._wait)
        self.start()

    def reload(self):
        '''
        Send the HUP signal to the underlying daemon
        '''
        cmds = ['killall', '-HUP', self._daemon]
        if self.status() is True :
            with open('/dev/null', 'w') as devnull:
                p = subprocess.Popen(cmds, stdout=devnull,
                    stderr=subprocess.PIPE)
                if p.wait() is not 0 :
                    raise Exception(p.communicate()[1])

