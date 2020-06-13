#!/usr/bin/env python

from pymongo import MongoClient, ASCENDING, DESCENDING

conn = MongoClient('localhost')

db = conn['fishtest_new']

db.drop_collection('pgns')

db.create_collection('pgns', capped=True, size=50000)
