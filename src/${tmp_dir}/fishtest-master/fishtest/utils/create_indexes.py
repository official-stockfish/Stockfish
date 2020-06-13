# create_indexes.py - (re-)create indexes
#
# Run this script manually to create the indexes, it could take a few
# seconds/minutes to run.

import os
import sys
import pprint

from pymongo import MongoClient, ASCENDING, DESCENDING

db_name = 'fishtest_new'

# MongoDB server is assumed to be on the same machine, if not user should use
# ssh with port forwarding to access the remote host.
conn = MongoClient(os.getenv('FISHTEST_HOST') or 'localhost')
db = conn[db_name]


def create_runs_indexes():
  print('Creating indexes on runs collection')
  db['runs'].create_index(
    [('last_updated', ASCENDING)],
    name='unfinished_runs',
    partialFilterExpression={ 'finished': False }
  )
  db['runs'].create_index(
    [('last_updated', DESCENDING)],
    name='finished_runs',
    partialFilterExpression={ 'finished': True }
  )
  db['runs'].create_index(
    [('last_updated', DESCENDING), ('is_green', DESCENDING)],
    name='finished_green_runs',
    partialFilterExpression={ 'finished': True, 'is_green': True }
  )
  db['runs'].create_index(
    [('last_updated', DESCENDING), ('is_yellow', DESCENDING)],
    name='finished_yellow_runs',
    partialFilterExpression={ 'finished': True, 'is_yellow': True }
  )
  db['runs'].create_index(
    [('last_updated', DESCENDING), ('tc_base', DESCENDING)],
    name='finished_ltc_runs',
    partialFilterExpression={ 'finished': True, 'tc_base': { '$gte': 40 } }
  )
  db['runs'].create_index(
    [('args.username', DESCENDING), ('last_updated', DESCENDING)],
    name='user_runs'
  )

def create_pgns_indexes():
  print('Creating indexes on pgns collection')
  db['pgns'].create_index([('run_id', DESCENDING)])

def create_users_indexes():
  db['users'].create_index('username', unique=True)

def create_actions_indexes():
  db['actions'].create_index([
    ('username', ASCENDING),
    ('_id', DESCENDING),
  ])
  db['actions'].create_index([
    ('action', ASCENDING),
    ('_id', DESCENDING),
  ])

def create_flag_cache_indexes():
  db['flag_cache'].create_index('ip')
  db['flag_cache'].create_index('geoip_checked_at', expireAfterSeconds=60 * 60 * 24 * 7)

def print_current_indexes():
  for collection_name in db.collection_names():
    c = db[collection_name]
    print('Current indexes on ' + collection_name + ':')
    pprint.pprint(c.index_information(), stream=None, indent=2, width=110, depth=None)
    print('')

def drop_indexes(collection_name):
  # Drop all indexes on collection except _id_
  print('\nDropping indexes on {}'.format(collection_name))
  collection = db[collection_name]
  index_keys = list(collection.index_information().keys())
  print('Current indexes: {}'.format(index_keys))
  for idx in index_keys:
    if idx != '_id_':
      print('Dropping ' + collection_name + ' index ' + idx + ' ...')
      collection.drop_index(idx)


if __name__ == '__main__':
  # Takes a list of collection names as arguments.
  # For each collection name, this script drops indexes and re-creates them.
  # With no argument, indexes are printed, but no indexes are re-created.
  collection_names = sys.argv[1:]
  if collection_names:
    print('Re-creating indexes...')
    for collection_name in collection_names:
      if collection_name == 'users':
        drop_indexes('users')
        create_users_indexes()
      elif collection_name == 'actions':
        drop_indexes('actions')
        create_actions_indexes()
      elif collection_name == 'runs':
        drop_indexes('runs')
        create_runs_indexes()
      elif collection_name == 'pgns':
        drop_indexes('pgns')
        create_pgns_indexes()
      elif collection_name == 'flag_cache':
        drop_indexes('flag_cache')
        create_flag_cache_indexes()
    print('Finished creating indexes!\n')
  print_current_indexes()
  if not collection_names:
    print('Collections in {}: {}'.format(db_name, db.collection_names()))
    print('Give a list of collection names as arguments to re-create indexes. For example:\n')
    print('  python3 create_indexes.py users runs - drops and creates indexes for runs and users\n')
