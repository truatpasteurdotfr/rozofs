Installation
============

Plugins Installation
--------------------

Under `plugins` directory are the RozoFS Nagios plugins to be copied under
`/usr/lib/nagios/plugins/` where your Nagios runs.

Commands Configuration File Installation
----------------------------------------

Under `cfg` directory stand files related to RozoFS plugins command definitions.
You should find the Nagios top configuration file on your Nagios server under
`/etc/nagios3/nagios.cfg`. Open it and insert at the end the following lines to
 include the RozoFS configuration files:

    cfg_file=/etc/nagios-plugins/config/rozofs-commands.cfg

or

    cfg_dir=/etc/nagios-plugins/config

Copy file `rozofs-commands.cfg` under `/etc/nagios-plugins/config/` directory.

This file defines the commands to invoke the different plugins and do not 
depends on your RozoFS cluster configuration.

Logos Installation
------------------

Copy all RozoFS logo files under `/usr/share/nagios/htdocs/images/logos/rozofs`
directory and make a symbolic link:

    # ln -sf  /usr/share/nagios/htdocs/images/logos/rozofs /usr/share/nagios3/htdocs/images/logos/rozofs


Plugins Description
===================

check_rozofs_volume.sh
----------------------

This plugin is used to monitor the state of a volume id using the `rozodiag`
interface. It gets as mandatory parameters the host where the `exportd` handling
this volume runs (VIP) and the acceptable critical/warning thresholds for the 
free volume space.

The plugin pings the host of the `exportd` and on success requests the `exportd`
for the volume statistics, and checks the status of the storage nodes servicing
the volume.

The result is **OK** when:

-   all storages nodes are up,

AND

-   the free volume space is over the warning threshold.

The result is **CRITICAL** when:

-   the free volume space is below the critical threshold,

OR

-   no storage node is up.

The result is **WARNING** in all other cases so:

-   the free volume space is between the warning and critical thresholds,

AND/OR

-   some storage nodes but not all are down.


check_rozofs_storaged.sh
------------------------

This plugin is used to monitor the state of a storage node using the `rozodiag`
interface. It gets as mandatory parameter the host of the storage node.

The plugin pings the storage node and then checks access to the `rozodiag`
interface of the `storaged` and each `storio` of the storage node.

The result is **OK** when:

-   the `storaged` and all the `storio` are up.

The result is **WARNING** when:

-   some `storio` is not responding.

The result is **CRITICAL** when:

-   the `storaged` is down or when all `storios` are down.


check_rozofs_rozofsmount.sh
---------------------------

This plugin is used to monitor the state of a RozoFS client using the 
`rozodiag` interface. It gets as mandatory parameters the host of the
RozoFS client.

The plugin pings the host of the RozoFS client.
It checks the status of the `rozofsmount` interface toward the `exportd`.
It checks the status of the `rozofsmount` interfaces toward its `storcli`.
It checks the status of the `storcli` interfaces toward the storage nodes.

The result is **OK** when:

-   the `rozofsmount` has its interface toward the `exportd` UP,

AND

-   the `rozofsmount` has its 2 interfaces toward the `storcli` UP,

AND

-   the one or two `storcli` have their interfaces toward the `storages` UP.

The result is **CRITICAL** when:

-   the `rozofsmount` is unreachable
  
OR

-   the `rozofsmount` has its interface toward the `exportd` DOWN,

OR

-   the `rozofsmount` has one of its interfaces toward a `storcli` DOWN,
  
OR

-   one of the `storcli` has more than one interface DOWN toward a storage node.


The result is **WARNING** when:

-   the `rozofsmount` has its interface UP toward the `exportd`,

AND

-   the `rozofsmount` has its 2 interfaces up toward the `storcli`,

AND

-   no `storcli` has more than one interface DOWN toward a storage node.

Example
=======

Under `examples` directory are examples of configuration files for a complete
RozoFS platform.
