#!/usr/bin/env python

# index_pending.py - create new index on runs.tasks.pending
#
# Run this script manually to create the index, it could take a few seconds/minutes
# to run.
#
# If the index needs to be removed for any reason, comment out the create_index()
# and uncomment the drop_index() and rerun.

from __future__ import print_function

import os
import sys
import pprint
from pymongo import MongoClient, ASCENDING, DESCENDING

db_name='fishtest_new'

# MongoDB server is assumed to be on the same machine, if not user should use
# ssh with port forwarding to access the remote host.
conn = MongoClient(os.getenv('FISHTEST_HOST') or 'localhost')
db = conn[db_name]
runs = db['runs']
pgns = db['pgns']

def printout(s):
  print(s)
  sys.stdout.flush()

# create indexes:
#printout("Creating index ...")
#runs.create_index([('tasks.pending', ASCENDING)])

printout("Creating pgn index ...")
pgns.ensure_index([('run_id', ASCENDING)])

# IF INDEX NEEDS TO BE DROPPED, COMMENT OUT ABOVE 2 LINES, AND UNCOMMENT NEXT 2:
# printout("\nDropping index ...")
# runs.drop_index('tasks.pending_1')

# display current list of indexes
printout("\nCurrent Indexes:")
pprint.pprint(runs.index_information(), stream=None, indent=1, width=80, depth=None)
pprint.pprint(pgns.index_information(), stream=None, indent=1, width=80, depth=None)
