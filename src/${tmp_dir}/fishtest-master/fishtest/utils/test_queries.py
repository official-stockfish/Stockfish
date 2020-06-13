
# test_queries.py - run some sample queries to check db speed
#

from __future__ import print_function

import os
import sys
import pprint
import time
from pymongo import MongoClient, ASCENDING, DESCENDING

sys.path.append(os.path.expanduser('~/fishtest/fishtest'))
from fishtest.rundb import RunDb


db_name='fishtest_new'
rundb = RunDb()

# MongoDB server is assumed to be on the same machine, if not user should use
# ssh with port forwarding to access the remote host.
conn = MongoClient(os.getenv('FISHTEST_HOST') or 'localhost')
db = conn[db_name]
runs = db['runs']
pgns = db['pgns']


def qlen(c):
  if (c): return len(list(c))
  else  : return 0

# Extra conditions that might be applied to finished_runs:
#     q['args.username'] = username
#     q['args.tc'] = {'$regex':'^([4-9][0-9])|([1-9][0-9][0-9])'}
#     q['results_info.style'] = '#44EB44'


# Time some important queries using call to rundb function

print("\nFetching unfinished runs ...")
start = time.time()
c = rundb.get_unfinished_runs()
end = time.time()

print("{} rows {:1.4f}".format(qlen(c), end-start) + "s\nFetching machines ...")
start = time.time()
c = rundb.get_machines()
end = time.time()

print("{} rows {:1.4f}".format(qlen(c), end-start) + "s\nFetching finished runs ...")
start = time.time()
c, n = rundb.get_finished_runs(skip=0, limit=50, username='',
                                                 success_only=False, ltc_only=False)
end = time.time()

print("{} rows {:1.4f}".format(qlen(c), end-start) + "s\nFetching finished runs (vdv) ...")
start = time.time()
c, n = rundb.get_finished_runs(skip=0, limit=50, username='vdv',
                                                 success_only=False, ltc_only=False)
end = time.time()

print("{} rows {:1.4f}".format(qlen(c), end-start) + "s\nRequesting pgn ...")
if (n == 0):
    c.append({'_id':'abc'})
start = time.time()
c = rundb.get_pgn(str(c[0]['_id']) + ".pgn")
end = time.time()


# Tests: Explain some queries - should show which indexes are being used

print("{} rows {:1.4f}".format(qlen(c), end-start) + "s")
print("\n\nExplain queries")
print("\nFetching unfinished runs xp ...")
start = time.time()
c = runs.find({'finished': False}, sort=[('last_updated', DESCENDING), ('start_time', DESCENDING)]).explain()
print(pprint.pformat(c, indent=3, width=110))
end = time.time()

print("{} rows {:1.4f}".format(qlen(c), end-start) + "s")
print("\nFetching machines xp ...")
start = time.time()
c = runs.find({'finished': False, 'tasks': {'$elemMatch': {'active': True}}}).explain()
print(pprint.pformat(c, indent=3, width=110))
end = time.time()

print("{} rows {:1.4f}".format(qlen(c), end-start) + "s")
print("\nFetching finished runs xp ...")
start = time.time()
q = {'finished': True}
c = runs.find(q, skip=0, limit=50, sort=[('last_updated', DESCENDING)]).explain()
print(pprint.pformat(c, indent=3, width=110))
end = time.time()

print("{} rows {:1.4f}".format(qlen(c), end-start) + "s")

