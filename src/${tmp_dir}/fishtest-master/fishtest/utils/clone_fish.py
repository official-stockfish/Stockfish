#!/usr/bin/env python

import requests
import bz2

from pymongo import MongoClient, ASCENDING, DESCENDING
from bson.binary import Binary

#fish_host = 'http://localhost:6543'
fish_host =  'http://94.198.98.239' # 'http://tests.stockfishchess.org'

conn = MongoClient('localhost')

#conn.drop_database('fish_clone')

db = conn['fish_clone']

pgndb = db['pgns']
runs = db['runs']

pgndb.ensure_index([('run_id', ASCENDING)])

def main():
  """clone a fishtest database with PGNs and runs with the REST API"""
  
  skip = 0
  count = 0
  in_sync = False
  loaded = {}
  while True:
    pgn_list = requests.get(fish_host + '/api/pgn_100/' + str(skip)).json()
    for pgn_file in pgn_list:
      print(pgn_file)
      if pgndb.find_one({'run_id': pgn_file}):
        print('Already copied: %s' % (pgn_file))
        if not pgn_file in loaded:
          in_sync = True
          break
      else:
        run_id = pgn_file.split('-')[0]
        if not runs.find_one({'_id': run_id}):
          print('New run: ' + run_id)
          run = requests.get(fish_host + '/api/get_run/' + run_id).json()
          runs.insert(run)
        pgn = requests.get(fish_host + '/api/pgn/' + pgn_file)
        pgndb.insert(dict(pgn_bz2=Binary(bz2.compress(pgn.content)), run_id= pgn_file))
        loaded[pgn_file] = True
        count += 1
    skip += len(pgn_list)
    if in_sync or len(pgn_list) < 100:
      break

  print('Copied:  %6d PGN files (~ %8d games)' % (count, 250 * count))
  count = pgndb.count()
  print('Database:%6d PGN files (~ %8d games)' % (count, 250 * count))
  count = runs.count()
  print('Database:%6d runs' % (count))
  
if __name__ == '__main__':
  main()
