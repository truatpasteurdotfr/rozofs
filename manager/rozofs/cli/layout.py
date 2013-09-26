# -*- coding: utf-8 -*-
from rozofs.core.platform import Platform
from rozofs.core.constants import LAYOUT_VALUES
from rozofs.cli.output import puts

def set(platform, args):
    platform.set_layout(args.layout[0])

def get(platform, args):
    layout = platform. get_layout()
    puts({
          "layout":layout,
          "inverse":LAYOUT_VALUES[layout][0],
          "forward":LAYOUT_VALUES[layout][1],
          "safe":LAYOUT_VALUES[layout][2]
        })

def dispatch(args):
    p = Platform(args.exportd)
    globals()[args.action.replace('-', '_')](p, args)
