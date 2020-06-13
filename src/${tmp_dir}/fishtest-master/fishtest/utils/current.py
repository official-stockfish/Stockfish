#!/usr/bin/env python

# current.py - list slow database queres currently running
#
# Run this script manually to list slow db queries. It also lists current indexes
# on the runs collection and runs the 'uptime' command to indicate how busy the
# system currently is.
#

from __future__ import print_function

import os
import pprint
import subprocess
import sys
from pymongo import MongoClient, ASCENDING, DESCENDING

db_name='fishtest_new'

# MongoDB server is assumed to be on the same machine, if not user should use
# ssh with port forwarding to access the remote host.
conn = MongoClient(os.getenv('FISHTEST_HOST') or 'localhost')
db = conn[db_name]
runs = db['runs']

def printout(s):
  print(s)
  sys.stdout.flush()

# display current list of indexes
printout("Current Indexes:")
pprint.pprint(runs.index_information(), stream=None, indent=1, width=80, depth=None)

# display current uptime command
printout("\nRun 'uptime':\n")
printout(subprocess.check_call(["uptime"]))

# display current operations
printout("\nCurrent operations:")
t = 0.3
if len(sys.argv) > 1:
  t = float(sys.argv[1])
pprint.pprint(db.current_op({'secs_running': {'$gte': t}, 'query': {'$ne': {}}}),
              stream=None, indent=1, width=80, depth=None)

