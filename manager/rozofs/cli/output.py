# -*- coding: utf-8 -*-
import sys
# import json
import yaml
from collections import OrderedDict


def order_rep(dumper, data):
    return dumper.represent_mapping(u'tag:yaml.org,2002:map', data.items(), flow_style=False)

def puts(ordered):
    yaml.add_representer(OrderedDict, order_rep)
    # print >> sys.stdout, json.dumps(obj, indent=4, separators=(',', ':'))
    sys.stdout.write(yaml.dump(OrderedDict(ordered)))
