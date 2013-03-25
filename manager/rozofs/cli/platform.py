# -*- coding: utf-8 -*-
import sys
from rozofs.core.platform import Platform, Role
from rozofs.core.agent import ServiceStatus

ROLES_STR = {Role.EXPORTD: "exportd", Role.STORAGED: "storaged", Role.ROZOFSMOUNT: "rozofsmount"}
STR_ROLES = {"exportd": Role.EXPORTD, "storaged": Role.STORAGED, "rozofsmount": Role.ROZOFSMOUNT}

def __roles_to_strings(roles):
    strs = []
    if roles & Role.EXPORTD == Role.EXPORTD:
        strs.append("exportd")
    if roles & Role.STORAGED == Role.STORAGED:
        strs.append("storaged")
    if roles & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT:
        strs.append("rozofsmount")
    return strs

#
# utility functions
#
def __single_line():
    print >> sys.stdout, 60 * '-'

def __double_line():
    print >> sys.stdout, 60 * '='

def __args_to_roles(args):
    roles = 0
    if args.roles:
        for r in args.roles:
            roles |= STR_ROLES[r]
    else:
        roles = Role.EXPORTD | Role.STORAGED | Role.ROZOFSMOUNT
    return roles

#
# general functions
#
# def set_exportd(platform, args):
#    platform.set_exportd_hostname(args.exportd[0])
#    print >> sys.stdout, "exportd hostname set to: %s" % (args.exportd)

def nodes(platform, args):
    nodes = platform.list_nodes(__args_to_roles(args))
    print >> sys.stdout, "%-20s %-20s" % ("NODE", "ROLES")
    for h, r in nodes.items():
        print >> sys.stdout, "%-20s %-20s" % (h, __roles_to_strings(r))


# def set_sharing(platform, args):
#    if "none" in args.protocols:
#        args.protocols = []
#    platform.set_sharing_protocols(args.protocols)
#    print >> sys.stdout, "protocol(s): %s  set" % (args.protocols)

#
# status related functions
#
def __service_status_string(service_status):
    return "running" if service_status else "not running"

def __print_host_statuses(host, statuses):
    if statuses is not None and not statuses:
        return

    if statuses is None:
        print >> sys.stdout, "NODE: %s - %s" % (host, 'DOWN')
        return

    print >> sys.stdout, "NODE: %s - %s" % (host, 'UP')
    print >> sys.stdout, "%-20s %-20s" % ('ROLE', 'STATUS')
    for r, s in statuses.items():
        role_name = ROLES_STR[r]
        role_status = __service_status_string(s)
        print >> sys.stdout, "%-20s %-20s" % (role_name, role_status)

def status(platform, args):
    statuses = platform.get_statuses(args.nodes, __args_to_roles(args))
    for h, s in statuses.items():
        __print_host_statuses(h, s)

def start(platform, args):
    platform.start(args.nodes, __args_to_roles(args))
#    if args.roles:
#        print >> sys.stdout, "platform: %s started." % args.roles
#    else:
#        print >> sys.stdout, "platform: started."


def stop(platform, args):
    platform.stop(args.nodes, __args_to_roles(args))

#
# configuration related functions
#
def __exportd_config_to_string(config):
    s = "\t\tLAYOUT: %d\n" % config.layout
    for v in config.volumes.values():
        s += "\t\tVOLUME: %d\n" % v.vid
        for c in v.clusters.values():
            s += "\t\t\tCLUSTER: %d\n" % c.cid
            s += "\t\t\t\t%-20s %-10s\n" % ('NODE', 'SID')
            for sid, h in c.storages.items():
                s += "\t\t\t\t%-20s %-10d\n" % (h, sid)
    if len(config.exports) != 0:
        s += "\t\t%-4s %-4s %-25s %-25s %-10s %-10s\n" % ('EID', 'VID', 'ROOT', 'MD5', 'SQUOTA', 'HQUOTA')
    for e in config.exports.values():
        s += "\t\t%-4d %-4d %-25s %-25s %-10s %-10s\n" % (e.eid, e.vid, e.root, e.md5, e.squota, e.hquota)

    return s

def __storaged_config_to_string(config):
    # s = "\t\tLAYOUT: %d\n" % config.layout
    s = "\t\tPORTS: %s\n" % config.ports
    s += "\t\t%-10s %-10s %-30s\n" % ('CID', 'SID', 'ROOT')
    keylist = config.storages.keys()
    keylist.sort()
    for key in keylist:
        st = config.storages[key]
        s += "\t\t%-10d %-10d %-30s\n" % (st.cid, st.sid, st.root)
    return s

def __rozofsmount_config_to_string(config):
    # s = "\t\tPROTOCOLS: %s\n" % config.protocols
    s = "\t\t%-20s %-20s\n" % ('NODE', 'EXPORT')
    for c in config:
        s += "\t\t%-20s %-20s\n" % (c.export_host, c.export_path)
    return s

def __print_host_configs(host, configurations):
    if configurations is not None and not configurations:
        return

    if configurations is None:
        print >> sys.stdout, "NODE: %s - %s" % (host, 'DOWN')
        return

    # __double_line()
    print >> sys.stdout, "NODE: %s - %s" % (host, 'UP')
    for r, c in configurations.items():
        # __single_line()
        print >> sys.stdout, "\tROLE: %s" % ROLES_STR[r]
        if (r & Role.EXPORTD == Role.EXPORTD):
            print >> sys.stdout, "%s" % __exportd_config_to_string(c)
        if (r & Role.STORAGED == Role.STORAGED):
            print >> sys.stdout, "%s" % __storaged_config_to_string(c)
        if (r & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT):
            print >> sys.stdout, "%s" % __rozofsmount_config_to_string(c)

def config(platform, args):
#    print >> sys.stdout, "EXPORTD HOST: %s, PROTOCOL(S): %s" % (platform.get_exportd_hostname(), platform.get_sharing_protocols())
    configurations = platform.get_configurations(args.nodes, __args_to_roles(args))
    for h, c in configurations.items():
        __print_host_configs(h, c)

#
# profilers related functions
#

# define P_COUNT     0
# define P_ELAPSE    1
# define P_BYTES     2
def __probe_to_string(ep, name):
    probe = getattr(ep, name)
    if probe[0] == 0:
        cpu = rate = 0
    else:
        rate = probe[0] * 1000000 / probe[1]
        cpu = probe[1] / probe[0]
    return "%-25s %-12d %-12d %-12d %-12s %-12s\n" % (name, probe[0], rate, cpu, "--", "--")

def __io_probe_to_string(ep, name):
    probe = getattr(ep, name)
    if probe[0] == 0:
        cpu = rate = throughput = 0
    else:
        rate = probe[0] * 1000000 / probe[1]
        cpu = probe[1] / probe[0]
        throughput = probe[2] / 1024 / 1024 * 1000000 / probe[1]
    return "%-25s %-12d %-12d %-12d %-12s %-12s\n" % (name, probe[0], rate, cpu, probe[1], throughput)

def __exportd_profiler_to_string(args, ep):
    if ep is None:
        return "\t\t NOT RUNNING"

    elapse = ep.now - ep.uptime
    days = elapse / 86400
    hours = (elapse / 3600) - (days * 24)
    mins = (elapse / 60) - (days * 1440) - (hours * 60)
    secs = elapse % 60
    s = "\t\texportd: %s - uptime: %d days, %d:%d:%d\n" % (ep.vers, days, hours, mins, secs)
    s += "\t\t%-25s %-12s %-12s %-12s %-12s %-12s\n" % ("OP", "CALL", "RATE(msg/s)", "CPU(us)", "COUNT(B)", "THROUGHPUT(MBps)")
    s += "\t\t" + __probe_to_string(ep, "ep_mount")
    s += "\t\t" + __probe_to_string(ep, "ep_umount")
    s += "\t\t" + __probe_to_string(ep, "ep_statfs")
    s += "\t\t" + __probe_to_string(ep, "ep_lookup")
    s += "\t\t" + __probe_to_string(ep, "ep_getattr")
    s += "\t\t" + __probe_to_string(ep, "ep_setattr")
    s += "\t\t" + __probe_to_string(ep, "ep_readlink")
    s += "\t\t" + __probe_to_string(ep, "ep_mknod")
    s += "\t\t" + __probe_to_string(ep, "ep_mkdir")
    s += "\t\t" + __probe_to_string(ep, "ep_unlink")
    s += "\t\t" + __probe_to_string(ep, "ep_rmdir")
    s += "\t\t" + __probe_to_string(ep, "ep_symlink")
    s += "\t\t" + __probe_to_string(ep, "ep_rename")
    s += "\t\t" + __probe_to_string(ep, "ep_readdir")
    s += "\t\t" + __io_probe_to_string(ep, "ep_read_block")
    s += "\t\t" + __io_probe_to_string(ep, "ep_write_block")
    s += "\t\t" + __probe_to_string(ep, "ep_link")
    s += "\t\t" + __probe_to_string(ep, "ep_setxattr")
    s += "\t\t" + __probe_to_string(ep, "ep_getxattr")
    s += "\t\t" + __probe_to_string(ep, "ep_removexattr")
    s += "\t\t" + __probe_to_string(ep, "ep_listxattr")
    s += "\t\t" + __probe_to_string(ep, "export_lv1_resolve_entry")
    s += "\t\t" + __probe_to_string(ep, "export_lv2_resolve_path")
    s += "\t\t" + __probe_to_string(ep, "export_lookup_fid")
    s += "\t\t" + __probe_to_string(ep, "export_update_files")
    s += "\t\t" + __probe_to_string(ep, "export_update_blocks")
    s += "\t\t" + __probe_to_string(ep, "export_stat")
    s += "\t\t" + __probe_to_string(ep, "export_lookup")
    s += "\t\t" + __probe_to_string(ep, "export_getattr")
    s += "\t\t" + __probe_to_string(ep, "export_setattr")
    s += "\t\t" + __probe_to_string(ep, "export_link")
    s += "\t\t" + __probe_to_string(ep, "export_mknod")
    s += "\t\t" + __probe_to_string(ep, "export_mkdir")
    s += "\t\t" + __probe_to_string(ep, "export_unlink")
    s += "\t\t" + __probe_to_string(ep, "export_rmdir")
    s += "\t\t" + __probe_to_string(ep, "export_symlink")
    s += "\t\t" + __probe_to_string(ep, "export_readlink")
    s += "\t\t" + __probe_to_string(ep, "export_rename")
    s += "\t\t" + __io_probe_to_string(ep, "export_read")
    s += "\t\t" + __probe_to_string(ep, "export_read_block")
    s += "\t\t" + __probe_to_string(ep, "export_write_block")
    s += "\t\t" + __probe_to_string(ep, "export_readdir")
    s += "\t\t" + __probe_to_string(ep, "export_setxattr")
    s += "\t\t" + __probe_to_string(ep, "export_getxattr")
    s += "\t\t" + __probe_to_string(ep, "export_removexattr")
    s += "\t\t" + __probe_to_string(ep, "export_listxattr")
    s += "\t\t" + __probe_to_string(ep, "lv2_cache_put")
    s += "\t\t" + __probe_to_string(ep, "lv2_cache_get")
    s += "\t\t" + __probe_to_string(ep, "lv2_cache_del")
    s += "\t\t" + __probe_to_string(ep, "volume_balance")
    s += "\t\t" + __probe_to_string(ep, "volume_distribute")
    s += "\t\t" + __probe_to_string(ep, "volume_stat")
    s += "\t\t" + __probe_to_string(ep, "mdir_open")
    s += "\t\t" + __probe_to_string(ep, "mdir_close")
    s += "\t\t" + __probe_to_string(ep, "mdir_read_attributes")
    s += "\t\t" + __probe_to_string(ep, "mdir_write_attributes")
    s += "\t\t" + __probe_to_string(ep, "mreg_open")
    s += "\t\t" + __probe_to_string(ep, "mreg_close")
    s += "\t\t" + __probe_to_string(ep, "mreg_read_attributes")
    s += "\t\t" + __probe_to_string(ep, "mreg_write_attributes")
    s += "\t\t" + __probe_to_string(ep, "mreg_read_dist")
    s += "\t\t" + __probe_to_string(ep, "mreg_write_dist")
    s += "\t\t" + __probe_to_string(ep, "mslnk_open")
    s += "\t\t" + __probe_to_string(ep, "mslnk_close")
    s += "\t\t" + __probe_to_string(ep, "mdir_close")
    s += "\t\t" + __probe_to_string(ep, "mslnk_read_attributes")
    s += "\t\t" + __probe_to_string(ep, "mslnk_read_link")
    s += "\t\t" + __probe_to_string(ep, "mslnk_write_link")
    return s

def __storaged_profiler_to_string(args, sps):
    if sps is None:
        return "\t\t NOT RUNNING"

    elapse = sps[0].now - sps[0].uptime
    days = elapse / 86400

    hours = (elapse / 3600) - (days * 24)
    mins = (elapse / 60) - (days * 1440) - (hours * 60)
    secs = elapse % 60
    s = "\t\tstoraged: %s - %d process(es), uptime: %d days, %d:%d:%d\n" % (
            sps[0].vers, len(sps) - 1, days, hours, mins, secs)

    s += "\t\t%-12s %-25s %-12s %-12s %-12s %-12s %-12s\n" % ("PORT", "OP",
            "CALL", "RATE(msg/s)", "CPU(us)", "COUNT(B)", "THROUGHPUT(MB/s)")

    s += "\t\t%-12s " % "--" + __probe_to_string(sps[0], "stat")
    s += "\t\t%-12s " % "--" + __probe_to_string(sps[0], "ports")
    s += "\t\t%-12s " % "--" + __probe_to_string(sps[0], "remove")

    # io processes
    i = 0
    for sp in sps[1:len(sps[0].io_process_ports) + 1]:
        s += "\t\t%-12d " % sps[0].io_process_ports[i] + __io_probe_to_string(sp, "read")
        s += "\t\t%-12d " % sps[0].io_process_ports[i] + __io_probe_to_string(sp, "write")
        s += "\t\t%-12d " % sps[0].io_process_ports[i] + __io_probe_to_string(sp, "truncate")
        i += 1

    # rb processes
    if sps[0].rb_process_ports :
        s += "\t\t%-12s %-12s %-12s %-16s %-16s\n" % ("PORT", "CID", "SID",
                "STATUS", "FILES REBUILT")
        i = 0
        for sp in sps[len(sps[0].ro_process_ports) + 1:]:
            s += "\t\t%-12d %-12d %-12d %-12d %-16s %d/%-12d" % (sps[0].rb_process_ports[i], sp.cid[i], sp.sid[i], "in progress", sp.rb_files_total, sp.rb_files_current)
            i += 1

    return s


def __mount_profiler_to_string(args, mp):
    if mp is None:
        return "\t\t NOT RUNNING"

    elapse = mp.now - mp.uptime
    days = elapse / 86400
    hours = (elapse / 3600) - (days * 24)
    mins = (elapse / 60) - (days * 1440) - (hours * 60)
    secs = elapse % 60
    s = "\t\trozofsmount: %s - uptime: %d days, %d:%d:%d\n" % (mp.vers, days, hours, mins, secs)

    s += "\t\t%-25s %-12s %-12s %-12s %-12s %-12s\n" % ("OP", "CALL", "RATE(msg/s)", "CPU(us)", "COUNT(B)", "THROUGHPUT(MBps)")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_lookup")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_forget")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_getattr")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_setattr")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_readlink")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_mknod")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_mkdir")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_unlink")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_rmdir")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_symlink")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_rename")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_open")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_link")
    s += "\t\t" + __io_probe_to_string(mp, "rozofs_ll_read")
    s += "\t\t" + __io_probe_to_string(mp, "rozofs_ll_write")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_flush")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_release")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_opendir")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_readdir")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_releasedir")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_fsyncdir")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_statfs")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_setxattr")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_getxattr")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_listxattr")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_removexattr")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_access")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_create")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_getlk")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_setlk")
    s += "\t\t" + __probe_to_string(mp, "rozofs_ll_ioctl")
    return s

def __print_host_profilers(args, host, profilers):
    if profilers is not None and not profilers:
        return

    if profilers is None:
        print >> sys.stdout, "NODE: %s - %s" % (host, 'DOWN')
        return

    print >> sys.stdout, "NODE: %s - %s" % (host, "UP")
    # __double_line()
    for r, p in profilers.items():
        # __single_line()
        print >> sys.stdout, "\tROLE: %s" % ROLES_STR[r]
        if (r & Role.EXPORTD == Role.EXPORTD):
            print >> sys.stdout, "%s" % __exportd_profiler_to_string(args, p)
        if (r & Role.STORAGED == Role.STORAGED):
            print >> sys.stdout, "%s" % __storaged_profiler_to_string(args, p)
        if (r & Role.ROZOFSMOUNT == Role.ROZOFSMOUNT):
            for mp in p:
                print >> sys.stdout, "%s" % __mount_profiler_to_string(args, mp)

def profile(platform, args):
    profilers = platform.get_profilers(args.nodes, __args_to_roles(args))
    for h, p in profilers.items():
        __print_host_profilers(args, h, p)

def layout(platform, args):
    platform.set_layout(args.layout[0])


def stat(platform, args):
    configurations = platform.get_configurations([args.exportd], Role.EXPORTD)

    if not configurations:
        print >> sys.stdout, "NODE: %s DOWN" % (args.exportd)
        return

    profilers = platform.get_profilers([args.exportd], Role.EXPORTD)
    if not profilers:
        print >> sys.stdout, "NODE: %s DOWN" % (args.exportd)
        return

    if not args.vids:
        args.vids = configurations[args.exportd][Role.EXPORTD].volumes.keys()

    for vstat in [vstat for vstat in profilers[args.exportd][Role.EXPORTD].vstats if vstat.vid in args.vids]:
        print >> sys.stdout, "VOLUME: %d - BSIZE: %d, BFREE: %d" % (vstat.vid, vstat.bsize, vstat.bfree)
        print >> sys.stdout, "\t%-12s %-12s %-20s %-20s" % ("NODE", "STATUS", "CAPACITY(B)", "FREE(B)")
        # get storages by host
        vconfig = configurations[args.exportd][Role.EXPORTD].volumes[vstat.vid]
        sstats = {}
        for sstat in vstat.sstats:
            # find the host with sid
            for c in vconfig.clusters.values():
                if sstat.sid in c.storages:
                    if c.storages[sstat.sid] in sstats:
                        sstats[c.storages[sstat.sid]][1] += sstat.size
                        sstats[c.storages[sstat.sid]][2] += sstat.free
                    else:
                        sstats[c.storages[sstat.sid]] = [sstat.status, sstat.size, sstat.free]
        for h, t in sstats.items():
            print >> sys.stdout, "\t%-12s %-12s %-20d %-20d" % (h, __service_status_string(t[0]), t[1], t[2])


        # print exported file systems stats
        print >> sys.stdout, "\n\t%-6s %-25s %-6s %-12s %-12s %-12s %-12s" % ("EID", "ROOT", "BSIZE", "BLOCKS", "BFREE", "FILES", "FFREE")
        for estat in [estat for estat in profilers[args.exportd][Role.EXPORTD].estats if estat.vid == vstat.vid]:
            # find the root

            print >> sys.stdout, "\t%-6d %-25s %-6d %-12d %-12d %-12d %-12d" % (
                    estat.eid, configurations[args.exportd][Role.EXPORTD].exports[estat.eid].root,
                    estat.bsize, estat.blocks, estat.bfree,
                    estat.files, estat.ffree)

def expand(platform, args):
    platform.add_nodes(args.hosts, args.vid)

def shrink(platform, args):
    for vid in args.vids:
        platform.remove_volume(vid)

def export(platform, args):
    platform.create_export(args.vid[0], args.name, args.passwd, args.squota, args.hquota)

def update(platform, args):
    platform.update_export(args.eid[0], args.current, args.passwd, args.squota, args.hquota)

def unexport(platform, args):
    if not args.eids:
        args.eids = None

    platform.remove_export(args.eids, args.force)

def mount(platform, args):
    if not args.eids:
        args.eids = None

    platform.mount_export(args.eids, args.nodes)

def umount(platform, args):
    if not args.eids:
        args.eids = None

    platform.umount_export(args.eids, args.nodes)

def platform_dispatch(args):
    p = Platform(args.exportd)
    globals()[args.command.replace('-', '_')](p, args)
