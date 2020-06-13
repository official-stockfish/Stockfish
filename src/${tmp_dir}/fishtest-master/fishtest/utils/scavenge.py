#!/usr/bin/env python

from __future__ import print_function

import os
import sys
from datetime import datetime, timedelta

# For tasks
sys.path.append(os.path.expanduser('~/fishtest/fishtest'))
from fishtest.rundb import RunDb

rundb = RunDb()

def scavenge_tasks(scavenge=True, minutes=60):
  """Check for tasks that have not been updated recently"""
  for run in rundb.runs.find({'tasks': {'$elemMatch': {'active': True}}}):
    changed = False
    for idx, task in enumerate(run['tasks']):
      if task['active'] and task['last_updated'] < datetime.utcnow() - timedelta(minutes=minutes):
        print('Scavenging', task)
        task['active'] = False
        changed = True
    if changed and scavenge:
      rundb.runs.save(run)

def main():
  scavenge_tasks(scavenge=True)

if __name__ == '__main__':
  main()
