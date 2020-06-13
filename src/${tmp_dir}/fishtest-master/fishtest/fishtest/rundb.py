import sys
import signal
import copy
import os
import random
import math
import time
import threading
import zlib
import re

from datetime import datetime, timedelta
from bson.objectid import ObjectId
from bson.binary import Binary
from pymongo import MongoClient, ASCENDING, DESCENDING

from fishtest.userdb import UserDb
from fishtest.actiondb import ActionDb

import fishtest.stats.stat_util
from fishtest.util import (
  calculate_residuals,
  estimate_game_duration,
  format_results,
  post_in_fishcooking_results,
  remaining_hours
)

last_rundb = None


class RunDb:
  def __init__(self, db_name='fishtest_new'):
    # MongoDB server is assumed to be on the same machine, if not user should
    # use ssh with port forwarding to access the remote host.
    self.conn = MongoClient(os.getenv('FISHTEST_HOST') or 'localhost')
    self.db = self.conn[db_name]
    self.userdb = UserDb(self.db)
    self.actiondb = ActionDb(self.db)
    self.pgndb = self.db['pgns']
    self.runs = self.db['runs']
    self.deltas = self.db['deltas']

    self.chunk_size = 200

    global last_rundb
    last_rundb = self

  def generate_tasks(self, num_games):
    tasks = []
    remaining = num_games
    while remaining > 0:
      task_size = min(self.chunk_size, remaining)
      tasks.append({
        'num_games': task_size,
        'pending': True,
        'active': False,
      })
      remaining -= task_size
    return tasks

  def new_run(self, base_tag, new_tag, num_games, tc, book, book_depth,
              threads, base_options, new_options,
              info='',
              resolved_base='',
              resolved_new='',
              msg_base='',
              msg_new='',
              base_signature='',
              new_signature='',
              rescheduled_from=None,
              base_same_as_master=None,
              start_time=None,
              sprt=None,
              spsa=None,
              username=None,
              tests_repo=None,
              auto_purge=True,
              throughput=100,
              priority=0):
    if start_time is None:
      start_time = datetime.utcnow()

    run_args = {
      'base_tag': base_tag,
      'new_tag': new_tag,
      'num_games': num_games,
      'tc': tc,
      'book': book,
      'book_depth': book_depth,
      'threads': threads,
      'resolved_base': resolved_base,
      'resolved_new': resolved_new,
      'msg_base': msg_base,
      'msg_new': msg_new,
      'base_options': base_options,
      'new_options': new_options,
      'info': info,
      'base_signature': base_signature,
      'new_signature': new_signature,
      'username': username,
      'tests_repo': tests_repo,
      'auto_purge': auto_purge,
      'throughput': throughput,
      'itp': 100,  # internal throughput
      'priority': priority,
    }

    if sprt is not None:
      run_args['sprt'] = sprt

    if spsa is not None:
      run_args['spsa'] = spsa

    tc_base = re.search('^(\d+(\.\d+)?)', tc)
    if tc_base:
      tc_base = float(tc_base.group(1))
    new_run = {
      'args': run_args,
      'start_time': start_time,
      'last_updated': start_time,
      'tc_base': tc_base,
      'base_same_as_master': base_same_as_master,
      # Will be filled in by tasks, indexed by task-id
      'tasks': self.generate_tasks(num_games),
      # Aggregated results
      'results': {'wins': 0, 'losses': 0, 'draws': 0},
      'results_stale': False,
      'finished': False,
      'approved': False,
      'approver': '',
    }

    if rescheduled_from:
      new_run['rescheduled_from'] = rescheduled_from

    return self.runs.insert_one(new_run).inserted_id

  def get_machines(self):
    machines = []
    active_runs = self.runs.find({
      'finished': False,
      'tasks': {
        '$elemMatch': {'active': True}
       }
    }, sort=[('last_updated', DESCENDING)])
    for run in active_runs:
      for task in run['tasks']:
        if task['active']:
          machine = copy.copy(task['worker_info'])
          machine['last_updated'] = task.get('last_updated', None)
          machine['run'] = run
          machine['nps'] = task.get('nps', 0)
          machines.append(machine)
    return machines

  def get_pgn(self, pgn_id):
    pgn_id = pgn_id.split('.')[0]  # strip .pgn
    pgn = self.pgndb.find_one({'run_id': pgn_id})
    if pgn:
      return zlib.decompress(pgn['pgn_zip']).decode()
    return None

  def get_pgn_100(self, skip):
    return [p['run_id'] for p in
            self.pgndb.find(skip=skip, limit=100, sort=[('_id', DESCENDING)])]

  # Cache runs
  run_cache = {}
  run_cache_lock = threading.Lock()
  run_cache_write_lock = threading.Lock()

  timer = None

  # handle termination
  def exit_run(signum, frame):
      global last_rundb
      if last_rundb:
        last_rundb.flush_all()
      sys.exit(0)

  signal.signal(signal.SIGINT, exit_run)
  signal.signal(signal.SIGTERM, exit_run)

  def get_run(self, r_id):
    with self.run_cache_lock:
      r_id = str(r_id)
      if r_id in self.run_cache:
        self.run_cache[r_id]['rtime'] = time.time()
        return self.run_cache[r_id]['run']
      try:
        run = self.runs.find_one({'_id': ObjectId(r_id)})
        if run:
          self.run_cache[r_id] = {'rtime': time.time(), 'ftime': time.time(),
                                  'run': run, 'dirty': False}
        return run
      except:
        return None

  def start_timer(self):
    self.timer = threading.Timer(1.0, self.flush_buffers)
    self.timer.start()

  def buffer(self, run, flush):
    with self.run_cache_lock:
      if self.timer is None:
        self.start_timer()
      r_id = str(run['_id'])
      if flush:
        self.run_cache[r_id] = {'dirty': False, 'rtime': time.time(),
                                'ftime': time.time(), 'run': run}
        with self.run_cache_write_lock:
          self.runs.replace_one({ '_id': ObjectId(r_id) }, run)
      else:
        if r_id in self.run_cache:
          ftime = self.run_cache[r_id]['ftime']
        else:
          ftime = time.time()
        self.run_cache[r_id] = {'dirty': True, 'rtime': time.time(),
                                'ftime': ftime, 'run': run}

  def stop(self):
    self.flush_all()
    with self.run_cache_lock:
      self.timer = None
    time.sleep(1.1)

  def flush_all(self):
    print("flush")
    # Note that we do not grab locks because this method is
    # called from a signal handler and grabbing locks might deadlock
    for r_id in list(self.run_cache):
      if self.run_cache[r_id]['dirty']:
        self.runs.replace_one({ '_id': ObjectId(r_id) }, self.run_cache[r_id]['run'])
        print(".", end='')
    print("done")

  def flush_buffers(self):
    if self.timer is None:
      return
    self.run_cache_lock.acquire()
    now = time.time()
    old = now + 1
    oldest = None
    for r_id in list(self.run_cache):
      if not self.run_cache[r_id]['dirty']:
        if self.run_cache[r_id]['rtime'] < now - 60:
          del self.run_cache[r_id]
      elif self.run_cache[r_id]['ftime'] < old:
        old = self.run_cache[r_id]['ftime']
        oldest = r_id
    if oldest is not None:
      if int(now) % 60 == 0:
        self.scavenge(self.run_cache[oldest]['run'])
      self.run_cache[oldest]['dirty'] = False
      self.run_cache[oldest]['ftime'] = time.time()
      self.run_cache_lock.release()  # Release the lock while writing
      # print("SYNC")
      with self.run_cache_write_lock:
        self.runs.save(self.run_cache[oldest]['run'])
      # start the timer when writing is done
      self.start_timer()
      return
    # Nothing to flush, start timer:
    self.start_timer()
    self.run_cache_lock.release()

  def scavenge(self, run):
    old = datetime.utcnow() - timedelta(minutes=30)
    for task in run['tasks']:
      if task['active'] and task['last_updated'] < old:
        task['active'] = False

  def get_unfinished_runs_id(self):
    with self.run_cache_write_lock:
      unfinished_runs = self.runs.find({'finished': False},
                                       {'_id': 1},
                                       sort=[('last_updated', DESCENDING)])
      return unfinished_runs

  def get_unfinished_runs(self, username=None):
    with self.run_cache_write_lock:
      unfinished_runs = self.runs.find({'finished': False},
                                       sort=[('last_updated', DESCENDING)])
      if username:
        unfinished_runs = [r for r in unfinished_runs if r['args'].get('username') == username]
      return unfinished_runs

  def aggregate_unfinished_runs(self, username=None):
    unfinished_runs = self.get_unfinished_runs(username)
    runs = {'pending': [], 'active': []}
    for run in unfinished_runs:
      state = 'active' if any(task['active'] for task in run['tasks']) else 'pending'
      runs[state].append(run)
    runs['pending'].sort(key=lambda run: (run['args']['priority'],
                                          run['args']['itp']
                                          if 'itp' in run['args'] else 100))
    runs['active'].sort(reverse=True, key=lambda run: (
        'sprt' in run['args'],
        run['args'].get('sprt',{}).get('llr',0),
        'spsa' not in run['args'],
        run['results']['wins'] + run['results']['draws']
        + run['results']['losses']))

    # Calculate but don't save results_info on runs using info on current machines
    cores = 0
    nps = 0
    for m in self.get_machines():
      concurrency = int(m['concurrency'])
      cores += concurrency
      nps += concurrency * m['nps']
    pending_hours = 0
    for run in runs['pending'] + runs['active']:
      if cores > 0:
        eta = remaining_hours(run) / cores
        pending_hours += eta
      results = self.get_results(run, False)
      run['results_info'] = format_results(results, run)
      if 'Pending...' in run['results_info']['info']:
        if cores > 0:
          run['results_info']['info'][0] += ' (%.1f hrs)' % (eta)
        if 'sprt' in run['args']:
          sprt = run['args']['sprt']
          elo_model = sprt.get('elo_model', 'BayesElo')
          if elo_model == 'BayesElo':
            run['results_info']['info'].append(('[%.2f,%.2f]')
                                              % (sprt['elo0'], sprt['elo1']))
          else:
            run['results_info']['info'].append(('{%.2f,%.2f}')
                                              % (sprt['elo0'], sprt['elo1']))
    return (runs, pending_hours, cores, nps)


  def get_finished_runs(self, skip=0, limit=0, username='',
                        success_only=False, yellow_only=False, ltc_only=False):
    q = {'finished': True}
    idx_hint = 'finished_runs'
    if username:
      q['args.username'] = username
      idx_hint = None
    if ltc_only:
      q['tc_base'] = {'$gte': 40}
      idx_hint = 'finished_ltc_runs'
    if success_only:
      q['is_green'] = True
      idx_hint = 'finished_green_runs'
    if yellow_only:
      q['is_yellow'] = True
      idx_hint = 'finished_yellow_runs'

    c = self.runs.find(q, skip=skip, limit=limit,
                       sort=[('last_updated', DESCENDING)])

    if idx_hint:
      # Use a fast COUNT_SCAN query when possible
      count = self.runs.estimated_document_count(hint=idx_hint)
    else:
      # Otherwise, the count is slow
      count = c.count()
    # Don't show runs that were deleted
    runs_list = [run for run in c if not run.get('deleted')]
    return [runs_list, count]

  def get_results(self, run, save_run=True):
    if not run['results_stale']:
      return run['results']

    results = {'wins': 0, 'losses': 0, 'draws': 0,
               'crashes': 0, 'time_losses': 0}

    has_pentanomial = True
    pentanomial = 5*[0]
    for task in run['tasks']:
      if 'stats' in task:
        stats = task['stats']
        results['wins'] += stats['wins']
        results['losses'] += stats['losses']
        results['draws'] += stats['draws']
        results['crashes'] += stats['crashes']
        results['time_losses'] += stats.get('time_losses', 0)
        if 'pentanomial' in stats.keys() and has_pentanomial:
          pentanomial = [pentanomial[i]+stats['pentanomial'][i]
                         for i in range(0, 5)]
        else:
          has_pentanomial = False
    if has_pentanomial:
      results['pentanomial'] = pentanomial

    run['results_stale'] = False
    run['results'] = results
    if save_run:
      self.buffer(run, True)

    return results

  def calc_itp(self, run):
    itp = run['args']['throughput']
    if itp < 1:
      itp = 1
    elif itp > 500:
      itp = 500
    itp *= math.sqrt(estimate_game_duration(run['args']['tc'])/estimate_game_duration('10+0.1'))
    itp *= math.sqrt(run['args']['threads'])
    if 'sprt' not in run['args']:
      itp *= 0.5
    else:
      llr = run['args']['sprt'].get('llr',0)
      itp *= (5 + llr) / 5
    run['args']['itp'] = itp

  def sum_cores(self, run):
    cores = 0
    for task in run['tasks']:
      if task['active']:
        cores += int(task['worker_info']['concurrency'])
    run['cores'] = cores

  # Limit concurrent request_task
  task_lock = threading.Lock()
  task_semaphore = threading.Semaphore(4)

  task_time = 0
  task_runs = None

  worker_runs = {}

  def request_task(self, worker_info):
    if self.task_semaphore.acquire(False):
      try:
        with self.task_lock:
          return self.sync_request_task(worker_info)
      finally:
        self.task_semaphore.release()
    else:
      print("request_task too busy")
      return {'task_waiting': False}

  def sync_request_task(self, worker_info):
    if time.time() > self.task_time + 60:
      self.task_runs = []
      for r in self.get_unfinished_runs_id():
        run = self.get_run(r['_id'])
        self.sum_cores(run)
        self.calc_itp(run)
        self.task_runs.append(run)
      self.task_runs.sort(key=lambda r: (-r['args']['priority'],
                          r['cores'] / r['args']['itp'] * 100.0,
                          -r['args']['itp'], r['_id']))
      self.task_time = time.time()

    max_threads = int(worker_info['concurrency'])
    min_threads = int(worker_info.get('min_threads', 1))
    max_memory = int(worker_info.get('max_memory', 0))

    # We need to allocate a new task, but first check we don't have the same
    # machine already running because multiple connections are not allowed.
    connections = 0
    for run in self.task_runs:
      for task in run['tasks']:
        if (task['active']
            and task['worker_info']['remote_addr']
                == worker_info['remote_addr']):
          connections = connections + 1

    # Allow a few connections, for multiple computers on same IP
    if connections >= self.userdb.get_machine_limit(worker_info['username']):
      return {'task_waiting': False, 'hit_machine_limit': True}

    # Limit worker Github API calls
    if 'rate' in worker_info:
      rate = worker_info['rate']
      limit = rate['remaining'] <= 2 * math.sqrt(rate['limit'])
    else:
      limit = False
    worker_key = worker_info['unique_key']

    # Get a new task that matches the worker requirements
    run_found = False
    for run in self.task_runs:
      # compute required TT memory
      need_tt = 0
      if max_memory > 0:
        def get_hash(s):
          h = re.search('Hash=([0-9]+)', s)
          if h:
            return int(h.group(1))
          return 0
        need_tt += get_hash(run['args']['new_options'])
        need_tt += get_hash(run['args']['base_options'])
        need_tt *= max_threads // run['args']['threads']

      if run['approved'] \
         and (not limit or (worker_key in self.worker_runs
                            and run['_id'] in self.worker_runs[worker_key])) \
         and run['args']['threads'] <= max_threads \
         and run['args']['threads'] >= min_threads \
         and need_tt <= max_memory:
        task_id = -1
        cores = 0
        if 'spsa' in run['args']:
          limit_cores = 40000 / math.sqrt(len(run['args']['spsa']['params']))
        else:
          limit_cores = 1000000  # No limit for SPRT
        for task in run['tasks']:
          if task['active']:
            cores += task['worker_info']['concurrency']
            if cores > limit_cores:
              break
          task_id = task_id + 1
          if not task['active'] and task['pending']:
            task['worker_info'] = worker_info
            task['last_updated'] = datetime.utcnow()
            task['active'] = True
            run_found = True
            break
      if run_found:
        break

    if not run_found:
      return {'task_waiting': False}

    self.sum_cores(run)
    self.task_runs.sort(key=lambda r: (-r['args']['priority'],
                        r['cores'] / r['args']['itp'] * 100.0,
                        -r['args']['itp'], r['_id']))

    self.buffer(run, False)

    # Update worker_runs (compiled tests)
    if worker_key not in self.worker_runs:
      self.worker_runs[worker_key] = {}
    if run['_id'] not in self.worker_runs[worker_key]:
      self.worker_runs[worker_key][run['_id']] = True

    return {'run': run, 'task_id': task_id}

  # Create a lock for each active run
  run_lock = threading.Lock()
  active_runs = {}
  purge_count = 0

  def active_run_lock(self, id):
    with self.run_lock:
      self.purge_count = self.purge_count + 1
      if self.purge_count > 100000:
        old = time.time() - 10000
        self.active_runs = dict(
            (k, v) for k, v in self.active_runs.items() if v['time'] >= old)
        self.purge_count = 0
      if id in self.active_runs:
        active_lock = self.active_runs[id]['lock']
        self.active_runs[id]['time'] = time.time()
      else:
        active_lock = threading.Lock()
        self.active_runs[id] = {'time': time.time(), 'lock': active_lock}
      return active_lock

  def update_task(self, run_id, task_id, stats, nps, spsa, username):
    lock = self.active_run_lock(str(run_id))
    with lock:
      return self.sync_update_task(run_id, task_id, stats, nps, spsa, username)

  def sync_update_task(self, run_id, task_id, stats, nps, spsa, username):
    run = self.get_run(run_id)
    if task_id >= len(run['tasks']):
      return {'task_alive': False}

    task = run['tasks'][task_id]
    if not task['active'] or not task['pending']:
      return {'task_alive': False}
    if task['worker_info']['username'] != username:
      print('Update_task: Non matching username: ' + username)
      return {'task_alive': False}

    # Guard against incorrect results
    count_games = lambda d: d['wins'] + d['losses'] + d['draws']
    num_games = count_games(stats)
    old_num_games = count_games(task['stats']) if 'stats' in task else 0
    spsa_games = count_games(spsa) if 'spsa' in run['args'] else 0
    if (num_games < old_num_games
        or (spsa_games > 0 and num_games <= 0)
        or (spsa_games > 0 and 'stats' in task and num_games <= old_num_games)
        ):
      return {'task_alive': False}
    if (num_games-old_num_games)%2!=0: # the worker should only runs game pairs
      return {'task_alive': False}
    if 'sprt' in run['args']:
      batch_size=2*run['args']['sprt'].get('batch_size',1)
      if num_games%batch_size != 0:
        return {'task_alive': False}

    all_tasks_finished = False

    task['stats'] = stats
    task['nps'] = nps
    if num_games >= task['num_games']:
      # This task is now finished
      if 'cores' in run:
        run['cores'] -= task['worker_info']['concurrency']
      task['pending'] = False  # Make pending False before making active false
                               # to prevent race in request_task
      task['active'] = False
      # Check if all tasks in the run have been finished
      if not any([t['pending'] or t['active'] for t in run['tasks']]):
        all_tasks_finished = True

    update_time = datetime.utcnow()
    task['last_updated'] = update_time
    run['last_updated'] = update_time
    run['results_stale'] = True

    # Update SPSA results
    if 'spsa' in run['args'] and spsa_games == spsa['num_games']:
      self.update_spsa(task['worker_info']['unique_key'], run, spsa)

    # Check SPRT state to decide whether to stop the run
    if 'sprt' in run['args']:
      sprt = run['args']['sprt']
      fishtest.stats.stat_util.update_SPRT(self.get_results(run, False), sprt)
      if sprt['state'] != '':
        # If SPRT is accepted or rejected, stop the run
        self.buffer(run, True)
        self.stop_run(run_id)
        return {'task_alive': False}

    if all_tasks_finished:
      # If all tasks are finished, stop the run
      self.buffer(run, True)
      self.stop_run(run_id)
    else:
      self.buffer(run, False)
    return {'task_alive': task['active']}

  def upload_pgn(self, run_id, pgn_zip):
    self.pgndb.insert_one({'run_id': run_id, 'pgn_zip': Binary(pgn_zip)})
    return {}

  def failed_task(self, run_id, task_id):
    run = self.get_run(run_id)
    if task_id >= len(run['tasks']):
      return {'task_alive': False}

    task = run['tasks'][task_id]
    if not task['active'] or not task['pending']:
      return {'task_alive': False}

    # Mark the task as inactive: it will be rescheduled
    task['active'] = False
    self.buffer(run, True)
    return {}

  def stop_run(self, run_id, run=None):
    """ Stops a run and runs auto-purge if it was enabled
        - Used by the website and API for manually stopping runs
        - Called during /api/update_task:
          - for stopping SPRT runs if the test is accepted or rejected
          - for stopping a run after all games are finished
    """
    self.clear_params(run_id)
    save_it = False
    if run is None:
      run = self.get_run(run_id)
      save_it = True
    run['tasks'] = [task for task in run['tasks'] if 'stats' in task]
    for task in run['tasks']:
      task['pending'] = False
      task['active'] = False
    if save_it:
      self.buffer(run, True)
      self.task_time = 0
      # Auto-purge runs here
      purged = False
      if run['args'].get('auto_purge', True) and 'spsa' not in run['args']:
        if self.purge_run(run):
          purged = True
          run = self.get_run(run['_id'])
          results = self.get_results(run, True)
          run['results_info'] = format_results(results, run)
          self.buffer(run, True)
      if not purged:
        # The run is now finished and will no longer be updated after this
        run['finished'] = True
        results = self.get_results(run, True)
        run['results_info'] = format_results(results, run)
        # De-couple the styling of the run from its finished status
        if run['results_info']['style'] == '#44EB44':
          run['is_green'] = True
        elif run['results_info']['style'] == 'yellow':
          run['is_yellow'] = True
        self.buffer(run, True)
        # Publish the results of the run to the Fishcooking forum
        post_in_fishcooking_results(run)

  def approve_run(self, run_id, approver):
    run = self.get_run(run_id)
    # Can't self approve
    if run['args']['username'] == approver:
      return False

    run['approved'] = True
    run['approver'] = approver
    self.buffer(run, True)
    self.task_time = 0
    return True

  def purge_run(self, run):
    # Remove bad tasks
    purged = False
    chi2 = calculate_residuals(run)
    if 'bad_tasks' not in run:
      run['bad_tasks'] = []
    for task in run['tasks']:
      if task['worker_key'] in chi2['bad_users']:
        purged = True
        task['bad'] = True
        run['bad_tasks'].append(task)
        run['tasks'].remove(task)
    if purged:
      # Generate new tasks if needed
      run['results_stale'] = True
      results = self.get_results(run)
      played_games = results['wins'] + results['losses'] + results['draws']
      if played_games < run['args']['num_games']:
        run['tasks'] += self.generate_tasks(
            run['args']['num_games'] - played_games)
      run['finished'] = False
      if 'sprt' in run['args'] and 'state' in run['args']['sprt']:
        fishtest.stats.stat_util.update_SPRT(results, run['args']['sprt'])
        run['args']['sprt']['state'] = ''
      self.buffer(run, True)
    return purged

  def spsa_param_clip_round(self, param, increment, clipping, rounding):
    if clipping == 'old':
      value = param['theta'] + increment
      if value < param['min']:
        value = param['min']
      elif value > param['max']:
        value = param['max']
    else:  # clipping == 'careful':
      inc = min(abs(increment), abs(param['theta'] - param['min']) / 2,
                abs(param['theta'] - param['max']) / 2)
      if inc > 0:
          value = param['theta'] + inc * increment / abs(increment)
      else:  # revert to old behavior to bounce off boundary
          value = param['theta'] + increment
          if value < param['min']:
            value = param['min']
          elif value > param['max']:
            value = param['max']

    # 'deterministic' rounding calls round() inside the worker.
    # 'randomized' says 4.p should be 5 with probability p,
    # 4 with probability 1-p,
    # and is continuous (albeit after expectation) unlike round().
    if rounding == 'randomized':
        value = math.floor(value + random.uniform(0, 1))

    return value

  # Store SPSA parameters for each worker
  spsa_params = {}

  def store_params(self, run_id, worker, params):
    run_id = str(run_id)
    if run_id not in self.spsa_params:
      self.spsa_params[run_id] = {}
    self.spsa_params[run_id][worker] = params

  def get_params(self, run_id, worker):
    run_id = str(run_id)
    if run_id not in self.spsa_params or worker not in self.spsa_params[run_id]:
      # Should only happen after server restart
      return self.generate_spsa(self.get_run(run_id))['w_params']
    return self.spsa_params[run_id][worker]

  def clear_params(self, run_id):
    run_id = str(run_id)
    if run_id in self.spsa_params:
      del self.spsa_params[run_id]

  def request_spsa(self, run_id, task_id):
    run = self.get_run(run_id)

    if task_id >= len(run['tasks']):
      return {'task_alive': False}
    task = run['tasks'][task_id]
    if not task['active'] or not task['pending']:
      return {'task_alive': False}

    result = self.generate_spsa(run)
    self.store_params(run['_id'], task['worker_info']['unique_key'],
                      result['w_params'])
    return result

  def generate_spsa(self, run):
    result = {
      'task_alive': True,
      'w_params': [],
      'b_params': [],
    }
    spsa = run['args']['spsa']
    if 'clipping' not in spsa:
        spsa['clipping'] = 'old'
    if 'rounding' not in spsa:
        spsa['rounding'] = 'deterministic'

    # Generate the next set of tuning parameters
    iter_local = spsa['iter'] + 1  # assume at least one completed,
                                   # and avoid division by zero
    for param in spsa['params']:
      c = param['c'] / iter_local ** spsa['gamma']
      flip = 1 if random.getrandbits(1) else -1
      result['w_params'].append({
        'name': param['name'],
        'value': self.spsa_param_clip_round(param, c * flip,
                                            spsa['clipping'], spsa['rounding']),
        'R': param['a'] / (spsa['A'] + iter_local) ** spsa['alpha'] / c ** 2,
        'c': c,
        'flip': flip,
      })
      result['b_params'].append({
        'name': param['name'],
        'value': self.spsa_param_clip_round(param, -c * flip, spsa['clipping'], spsa['rounding']),
      })

    return result

  def update_spsa(self, worker, run, spsa_results):
    spsa = run['args']['spsa']
    if 'clipping' not in spsa:
        spsa['clipping'] = 'old'

    spsa['iter'] += int(spsa_results['num_games'] / 2)

    # Store the history every 'freq' iterations.
    # More tuned parameters result in a lower update frequency,
    # so that the required storage (performance) remains constant.
    if 'param_history' not in spsa:
      spsa['param_history'] = []
    L = len(spsa['params'])
    freq = L * 25
    if freq < 100:
      freq = 100
    maxlen = 250000 / freq
    grow_summary = len(spsa['param_history']) < min(maxlen, spsa['iter'] / freq)

    # Update the current theta based on the results from the worker
    # Worker wins/losses are always in terms of w_params
    result = spsa_results['wins'] - spsa_results['losses']
    summary = []
    w_params = self.get_params(run['_id'], worker)
    for idx, param in enumerate(spsa['params']):
      R = w_params[idx]['R']
      c = w_params[idx]['c']
      flip = w_params[idx]['flip']
      param['theta'] = self.spsa_param_clip_round(param, R * c * result * flip,
                                                  spsa['clipping'],
                                                  'deterministic')
      if grow_summary:
        summary.append({
          'theta': param['theta'],
          'R': R,
          'c': c,
        })

    if grow_summary:
      spsa['param_history'].append(summary)
