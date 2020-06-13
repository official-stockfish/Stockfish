import base64
from datetime import datetime

import requests
from pyramid.httpexceptions import HTTPUnauthorized, exception_response
from pyramid.view import view_config, view_defaults, exception_view_config
from pyramid.response import Response

from fishtest.stats.stat_util import SPRT_elo

WORKER_VERSION = 79

flag_cache = {}


def strip_run(run):
  run = run.copy()
  if 'tasks' in run:
    del run['tasks']
  if 'bad_tasks' in run:
    del run['bad_tasks']
  if 'spsa' in run['args'] and 'param_history' in run['args']['spsa']:
    del run['args']['spsa']['param_history']
  run['_id'] = str(run['_id'])
  run['start_time'] = str(run['start_time'])
  run['last_updated'] = str(run['last_updated'])
  return run


@exception_view_config(HTTPUnauthorized)
def authentication_failed(error, request):
  response = Response(json_body=error.detail)
  response.status_int = 401
  return response


@view_defaults(renderer='json')
class ApiView(object):
  ''' All API endpoints that require authentication are used by workers '''

  def __init__(self, request):
    self.request = request

  def require_authentication(self):
    token = self.request.userdb.authenticate(self.get_username(),
                                             self.request.json_body['password'])
    if 'error' in token:
      raise HTTPUnauthorized(token)

  def get_username(self):
    if 'username' in self.request.json_body:
      return self.request.json_body['username']
    return self.request.json_body['worker_info']['username']

  def get_flag(self):
    ip = self.request.remote_addr
    if ip in flag_cache:
      return flag_cache.get(ip, None)  # Handle race condition on "del"
    # concurrent invocations get None, race condition is not an issue
    flag_cache[ip] = None
    result = self.request.userdb.flag_cache.find_one({'ip': ip})
    if result:
      flag_cache[ip] = result['country_code']
      return result['country_code']
    try:
      # Get country flag from worker IP address
      FLAG_HOST = 'https://freegeoip.app/json/'
      r = requests.get(FLAG_HOST + self.request.remote_addr, timeout=1.0)
      if r.status_code == 200:
        country_code = r.json()['country_code']
        self.request.userdb.flag_cache.insert_one({
          'ip': ip,
          'country_code': country_code,
          'geoip_checked_at': datetime.utcnow()
        })
        flag_cache[ip] = country_code
        return country_code
      raise Error("flag server failed")
    except:
      del flag_cache[ip]
      print('Failed GeoIP check for {}'.format(ip))
      return None

  def run_id(self):
    return str(self.request.json_body['run_id'])

  def task_id(self):
    return int(self.request.json_body['task_id'])


  @view_config(route_name='api_active_runs')
  def active_runs(self):
    active = {}
    for run in self.request.rundb.get_unfinished_runs():
      active[str(run['_id'])] = strip_run(run)
    return active


  @view_config(route_name='api_get_run')
  def get_run(self):
    run = self.request.rundb.get_run(self.request.matchdict['id'])
    return strip_run(run)


  @view_config(route_name='api_get_elo')
  def get_elo(self):
    run = self.request.rundb.get_run(self.request.matchdict['id']).copy()
    results = run['results']
    if 'sprt' not in run['args']:
      return {}
    sprt = run['args'].get('sprt').copy()
    elo_model = sprt.get('elo_model', 'BayesElo')
    alpha = sprt['alpha']
    beta = sprt['beta']
    elo0 = sprt['elo0']
    elo1 = sprt['elo1']
    sprt['elo_model'] = elo_model
    a = SPRT_elo(results,
                 alpha=alpha, beta=beta,
                 elo0=elo0, elo1=elo1,
                 elo_model=elo_model)
    run = strip_run(run)
    run['elo'] = a
    run['args']['sprt'] = sprt
    return run


  @view_config(route_name='api_request_task')
  def request_task(self):
    self.require_authentication()

    worker_info = self.request.json_body['worker_info']
    worker_info['remote_addr'] = self.request.remote_addr
    flag = self.get_flag()
    if flag:
      worker_info['country_code'] = flag

    result = self.request.rundb.request_task(worker_info)
    if 'task_waiting' in result:
      return result

    # Strip the run of unneccesary information
    run = result['run']
    min_run = {
      '_id': str(run['_id']),
      'args': run['args'],
      'tasks': [],
    }
    if int(str(worker_info['version']).split(':')[0]) > 64:
      task = run['tasks'][result['task_id']]
      min_task = {'num_games': task['num_games']}
      if 'stats' in task:
        min_task['stats'] = task['stats']
      min_run['my_task'] = min_task
    else:
      for task in run['tasks']:
        min_task = {'num_games': task['num_games']}
        if 'stats' in task:
          min_task['stats'] = task['stats']
        min_run['tasks'].append(min_task)

    result['run'] = min_run
    return result


  @view_config(route_name='api_update_task')
  def update_task(self):
    self.require_authentication()
    return self.request.rundb.update_task(
      run_id=self.run_id(),
      task_id=self.task_id(),
      stats=self.request.json_body['stats'],
      nps=self.request.json_body.get('nps', 0),
      spsa=self.request.json_body.get('spsa', {}),
      username=self.get_username()
    )


  @view_config(route_name='api_failed_task')
  def failed_task(self):
    self.require_authentication()
    return self.request.rundb.failed_task(self.run_id(), self.task_id())


  @view_config(route_name='api_upload_pgn')
  def upload_pgn(self):
    self.require_authentication()
    return self.request.rundb.upload_pgn(
      run_id='{}-{}'.format(self.run_id(), self.task_id()),
      pgn_zip=base64.b64decode(self.request.json_body['pgn'])
    )


  @view_config(route_name='api_download_pgn', renderer='string')
  def download_pgn(self):
    pgn = self.request.rundb.get_pgn(self.request.matchdict['id'])
    if pgn is None:
      raise exception_response(404)
    if '.pgn' in self.request.matchdict['id']:
      self.request.response.content_type = 'application/x-chess-pgn'
    return pgn


  @view_config(route_name='api_download_pgn_100')
  def download_pgn_100(self):
    skip = int(self.request.matchdict['skip'])
    urls = self.request.rundb.get_pgn_100(skip)
    if urls is None:
      raise exception_response(404)
    return urls


  @view_config(route_name='api_stop_run')
  def stop_run(self):
    self.require_authentication()
    username = self.get_username()
    user = self.request.userdb.user_cache.find_one({'username': username})
    if not user or user['cpu_hours'] < 1000:
      return {}
    with self.request.rundb.active_run_lock(self.run_id()):
      run = self.request.rundb.get_run(self.run_id())
      run['finished'] = True
      run['stop_reason'] = self.request.json_body.get('message', 'API request')
      self.request.actiondb.stop_run(username, run)
      self.request.rundb.stop_run(self.run_id())
    return {}


  @view_config(route_name='api_request_version')
  def request_version(self):
    self.require_authentication()
    return {'version': WORKER_VERSION}


  @view_config(route_name='api_request_spsa')
  def request_spsa(self):
    self.require_authentication()
    return self.request.rundb.request_spsa(self.run_id(), self.task_id())
