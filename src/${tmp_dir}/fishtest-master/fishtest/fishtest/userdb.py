import sys
import time
import threading

from datetime import datetime
from pymongo import ASCENDING


class UserDb:
  def __init__(self, db):
    self.db = db
    self.users = self.db['users']
    self.user_cache = self.db['user_cache']
    self.top_month = self.db['top_month']
    self.flag_cache = self.db['flag_cache']

  # Cache user lookups for 60s
  user_lock = threading.Lock()
  cache = {}

  def find(self, name):
    with self.user_lock:
      if name in self.cache:
        u = self.cache[name]
        if u['time'] > time.time() - 60:
          return u['user']
      user = self.users.find_one({'username': name})
      if not user:
        return None
      self.cache[name] = {'user': user, 'time': time.time()}
      return user

  def clear_cache(self):
    with self.user_lock:
      self.cache.clear()

  def authenticate(self, username, password):
    user = self.find(username)
    if not user or user['password'] != password:
      sys.stderr.write('Invalid login: "%s" "%s"\n' % (username, password))
      return {'error': 'Invalid password'}
    if 'blocked' in user and user['blocked']:
      sys.stderr.write('Blocked login: "%s" "%s"\n' % (username, password))
      return {'error': 'Blocked'}

    return {'username': username, 'authenticated': True}

  def get_users(self):
    return self.users.find(sort=[('_id', ASCENDING)])

  # Cache pending for 1s
  last_pending_time = 0
  last_pending = None
  pending_lock = threading.Lock()

  def get_pending(self):
    with self.pending_lock:
      if time.time() > self.last_pending_time + 1:
        self.last_pending = list(self.users.find({'blocked': True},
                                                 sort=[('_id', ASCENDING)]))
        self.last_pending_time = time.time()
      return self.last_pending

  def get_user(self, username):
    return self.find(username)

  def get_user_groups(self, username):
    user = self.find(username)
    if user:
      groups = user['groups']
      return groups

  def add_user_group(self, username, group):
    user = self.find(username)
    user['groups'].append(group)
    self.users.save(user)

  def create_user(self, username, password, email):
    try:
      if self.find(username):
        return False
      self.users.insert_one({
        'username': username,
        'password': password,
        'registration_time': datetime.utcnow(),
        'blocked': True,
        'email': email,
        'groups': [],
        'tests_repo': ''
      })
      self.last_pending_time = 0

      return True
    except:
      return False

  def save_user(self, user):
    self.users.replace_one({ 'username': user['username'] }, user)
    self.last_pending_time = 0

  def get_machine_limit(self, username):
    user = self.find(username)
    if user and 'machine_limit' in user:
      return user['machine_limit']
    return 16
