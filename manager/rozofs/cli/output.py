# -*- coding: utf-8 -*-
import sys
# import json
import yaml

def puts(obj):
    # print >> sys.stdout, json.dumps(obj, indent=4, separators=(',', ':'))
    sys.stdout.write(yaml.dump(obj))
