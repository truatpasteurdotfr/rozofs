# -*- coding: utf-8 -*-
from rozofs.core.platform import Platform
from rozofs.core.constants import LAYOUT_VALUES
import sys

def set(platform, args):
    platform.set_layout(args.layout[0])

def get(platform, args):
    layout = platform. get_layout()
    print >> sys.stdout, "layout=%d" % layout
    print >> sys.stdout, "inverse=%d" % LAYOUT_VALUES[layout][0]
    print >> sys.stdout, "forward=%d" % LAYOUT_VALUES[layout][1]
    print >> sys.stdout, "safe=%d" % LAYOUT_VALUES[layout][2]

def dispatch(args):
    p = Platform(args.exportd)
    globals()[args.action.replace('-', '_')](p, args)
