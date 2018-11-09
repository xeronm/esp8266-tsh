#!/usr/bin/python
# -*- coding: utf-8 -*-

import json
import os
import sys

if len(sys.argv) < 3:
    print("Error: Invalid arguments\nUsage: bininfo.py <image_info_file> [image_info_file ...] <bundle_info_file>")
    exit()

data = []
for filename in sys.argv[1:-1]:
    if not os.path.isfile(filename):
        print("File not found: %s" % filename)
        exit(-1)
    with open(filename, 'r') as f:
        data.append(json.loads(f.read()))

filename = sys.argv[-1]
with open(filename, 'w') as f:
    print("Writing bundle info file: %s" % filename)
    f.write( json.dumps(data, indent=4, separators=(',', ': ')) )
