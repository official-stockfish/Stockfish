from datetime import datetime
from pymongo import DESCENDING


class ActionDb:
  def __init__(self, db):
    self.db = db
    self.actions = self.db['actions']

  def get_actions(self, max_num, action=None, username=None):
    q = {}
    if action:
      q['action'] = action
    else:
      q['action'] = {"$ne": 'update_stats'}
    if username:
      q['username'] = username
    return self.actions.find(q, sort=[('_id', DESCENDING)], limit=max_num)

  def update_stats(self):
    self._new_action('fishtest.system', 'update_stats', '')

  def new_run(self, username, run):
    self._new_action(username, 'new_run', run)

  def modify_run(self, username, before, after):
    self._new_action(username, 'modify_run',
                     {'before': before, 'after': after})

  def delete_run(self, username, run):
    self._new_action(username, 'delete_run', run)

  def stop_run(self, username, run):
    self._new_action(username, 'stop_run', run)

  def approve_run(self, username, run):
    self._new_action(username, 'approve_run', run)

  def purge_run(self, username, run):
    self._new_action(username, 'purge_run', run)

  def block_user(self, username, data):
    self._new_action(username, 'block_user', data)

  def _new_action(self, username, action, data):
    self.actions.insert_one({
      'username': username,
      'action': action,
      'data': data,
      'time': datetime.utcnow(),
    })
