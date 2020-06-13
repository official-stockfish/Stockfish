import copy
import datetime
import os
import time
import threading
import re
import html

import requests
from pyramid.security import remember, forget, authenticated_userid, has_permission
from pyramid.view import view_config, forbidden_view_config
from pyramid.httpexceptions import HTTPFound, exception_response
from pyramid.response import Response

import fishtest.stats.stat_util
from fishtest.util import calculate_residuals, format_results, estimate_game_duration, delta_date


def clear_cache():
  global last_time, last_tests
  building.acquire()
  last_time = 0
  last_tests = None
  building.release()


def cached_flash(request, requestString):
  clear_cache()
  request.session.flash(requestString)
  return


@view_config(route_name='home')
def home(request):
  return HTTPFound(location=request.route_url('tests'))


@view_config(route_name='login', renderer='login.mak',
             require_csrf=True, request_method=('GET', 'POST'))
@forbidden_view_config(renderer='login.mak')
def login(request):
  login_url = request.route_url('login')
  referrer = request.url
  if referrer == login_url:
    referrer = '/'  # never use the login form itself as came_from
  came_from = request.params.get('came_from', referrer)

  if request.method == 'POST':
    username = request.POST.get('username')
    password = request.POST.get('password')
    token = request.userdb.authenticate(username, password)
    if 'error' not in token:
      if request.POST.get('stay_logged_in'):
        # Session persists for a year after login
        headers = remember(request, username, max_age=60 * 60 * 24 * 365)
      else:
        # Session ends when the browser is closed
        headers = remember(request, username)
      next_page = request.params.get('next') or came_from
      return HTTPFound(location=next_page, headers=headers)

    request.session.flash(token['error'], 'error')  # 'Incorrect password'
  return {}


@view_config(route_name='logout', require_csrf=True, request_method='POST')
def logout(request):
  session = request.session
  headers = forget(request)
  session.invalidate()
  return HTTPFound(location=request.route_url('tests'), headers=headers)


@view_config(route_name='signup', renderer='signup.mak',
             require_csrf=True, request_method=('GET', 'POST'))
def signup(request):
  if request.method != 'POST':
    return {}
  errors = []
  if len(request.POST.get('password', '')) == 0:
    errors.append('Non-empty password required')
  if request.POST.get('password') != request.POST.get('password2', ''):
    errors.append('Matching verify password required')
  if '@' not in request.POST.get('email', ''):
    errors.append('Email required')
  if len(request.POST.get('username', '')) == 0:
    errors.append('Username required')
  if not request.POST.get('username', '').isalnum():
    errors.append('Alphanumeric username required')
  if errors:
    for error in errors:
      request.session.flash(error, 'error')
    return {}

  path = os.path.expanduser('~/fishtest.captcha.secret')
  if os.path.exists(path):
    with open(path, 'r') as f:
      secret = f.read()
      payload = {'secret': secret,
                 'response': request.POST.get('g-recaptcha-response', ''),
                 'remoteip': request.remote_addr}
      response = requests.post(
          'https://www.google.com/recaptcha/api/siteverify',
          data=payload).json()
      if 'success' not in response or not response['success']:
        if 'error-codes' in response:
          print(response['error-codes'])
        request.session.flash('Captcha failed', 'error')
        return {}

  result = request.userdb.create_user(
    username=request.POST.get('username', ''),
    password=request.POST.get('password', ''),
    email=request.POST.get('email', '')
  )
  if not result:
    request.session.flash('Invalid username', 'error')
  else:
    request.session.flash(
        'Your account will be activated by an administrator soon...')
    return HTTPFound(location=request.route_url('login'))
  return {}


@view_config(route_name='actions', renderer='actions.mak')
def actions(request):
  search_action = request.params.get('action', '')
  search_user = request.params.get('user', '')

  actions_list = []
  for action in request.actiondb.get_actions(100, search_action, search_user):
    item = {
      'action': action['action'],
      'time': action['time'],
      'username': action['username'],
    }
    if action['action'] == 'update_stats':
      item['user'] = ''
      item['description'] = 'Update user statistics'
    elif action['action'] == 'block_user':
      item['description'] = (
          'blocked' if action['data']['blocked'] else 'unblocked')
      item['user'] = action['data']['user']
    elif action['action'] == 'modify_run':
      item['run'] = action['data']['before']['args']['new_tag']
      item['_id'] = action['data']['before']['_id']
      item['description'] = []

      before = action['data']['before']['args']['priority']
      after = action['data']['after']['args']['priority']
      if before != after:
        item['description'].append(
            'priority changed from {} to {}'.format(before, after))

      before = action['data']['before']['args']['num_games']
      after = action['data']['after']['args']['num_games']
      if before != after:
        item['description'].append(
            'games changed from {} to {}'.format(before, after))

      before = action['data']['before']['args']['throughput']
      after = action['data']['after']['args']['throughput']
      if before != after:
        item['description'].append(
            'throughput changed from {} to {}'.format(before, after))

      before = action['data']['before']['args']['auto_purge']
      after = action['data']['after']['args']['auto_purge']
      if before != after:
        item['description'].append(
            'auto-purge changed from {} to {}'.format(before, after))

      item['description'] = 'modify: ' + ', '.join(item['description'])
    else:
      item['run'] = action['data']['args']['new_tag']
      item['_id'] = action['data']['_id']
      item['description'] = ' '.join(action['action'].split('_'))
      if action['action'] == 'stop_run':
        item['description'] += ': {}'.format(
            action['data'].get('stop_reason', 'User stop'))

    actions_list.append(item)

  return {'actions': actions_list,
          'approver': has_permission('approve_run', request.context, request)}


def get_idle_users(request):
  idle = {}
  for u in request.userdb.get_users():
    idle[u['username']] = u
  for u in request.userdb.user_cache.find():
    del idle[u['username']]
  idle = list(idle.values())
  return idle


@view_config(route_name='pending', renderer='pending.mak')
def pending(request):
  if not has_permission('approve_run', request.context, request):
    request.session.flash('You cannot view pending users', 'error')
    return HTTPFound(location=request.route_url('tests'))

  return {'users': request.userdb.get_pending(),
          'idle': get_idle_users(request)}


@view_config(route_name='user', renderer='user.mak')
@view_config(route_name='profile', renderer='user.mak')
def user(request):
  userid = authenticated_userid(request)
  if not userid:
    request.session.flash('Please login')
    return HTTPFound(location=request.route_url('login'))
  user_name = request.matchdict.get('username', userid)
  profile = (user_name == userid)
  if not profile and not has_permission(
      'approve_run', request.context, request):
    request.session.flash('You cannot inspect users', 'error')
    return HTTPFound(location=request.route_url('tests'))
  user_data = request.userdb.get_user(user_name)
  if 'user' in request.POST:
    if profile:
      if len(request.params.get('password')) > 0:
        if (request.params.get('password')
            != request.params.get('password2', '')):
          request.session.flash('Matching verify password required', 'error')
          return {'user': user_data, 'profile': profile}
        user_data['password'] = request.params.get('password')
      if len(request.params.get('email')) > 0:
        user_data['email'] = request.params.get('email')
    else:
      user_data['blocked'] = ('blocked' in request.POST)
      request.userdb.last_pending_time = 0
      request.actiondb.block_user(authenticated_userid(request),
                              {'user': user_name, 'blocked': user_data['blocked']})
      request.session.flash(('Blocked' if user_data['blocked'] else 'Unblocked')
                            + ' user ' + user_name)
    request.userdb.save_user(user_data)
    return HTTPFound(location=request.route_url('tests'))
  userc = request.userdb.user_cache.find_one({'username': user_name})
  hours = int(userc['cpu_hours']) if userc is not None else 0
  return {'user': user_data, 'limit': request.userdb.get_machine_limit(user_name),
          'hours': hours, 'profile': profile}


@view_config(route_name='users', renderer='users.mak')
def users(request):
  users_list = list(request.userdb.user_cache.find())
  users_list.sort(key=lambda k: k['cpu_hours'], reverse=True)
  return {'users': users_list}


@view_config(route_name='users_monthly', renderer='users.mak')
def users_monthly(request):
  users_list = list(request.userdb.top_month.find())
  users_list.sort(key=lambda k: k['cpu_hours'], reverse=True)
  return {'users': users_list}


def get_master_bench():
  bs = re.compile(r'(^|\s)[Bb]ench[ :]+([0-9]{7})', re.MULTILINE)
  for c in requests.get(
      'https://api.github.com/repos/official-stockfish/Stockfish/commits').json():
    if not 'commit' in c:
      return None
    m = bs.search(c['commit']['message'])
    if m:
      return m.group(2)
  return None


def get_sha(branch, repo_url):
  """ Resolves the git branch to sha commit """
  api_url = repo_url.replace('https://github.com',
                             'https://api.github.com/repos')
  try:
    commit = requests.get(api_url + '/commits/' + branch).json()
  except:
    raise Exception("Unable to access developer repository")
  if 'sha' in commit:
    return commit['sha'], commit['commit']['message'].split('\n')[0]
  else:
    return '', ''


def parse_spsa_params(raw, spsa):
  params = []
  for line in raw.split('\n'):
    chunks = line.strip().split(',')
    if len(chunks) == 0:
      continue
    if len(chunks) != 6:
      raise Exception('"%s" needs 6 parameters"' % (line))
    param = {
      'name': chunks[0],
      'start': float(chunks[1]),
      'min': float(chunks[2]),
      'max': float(chunks[3]),
      'c_end': float(chunks[4]),
      'r_end': float(chunks[5]),
    }
    param['c'] = param['c_end'] * spsa['num_iter'] ** spsa['gamma']
    param['a_end'] = param['r_end'] * param['c_end'] ** 2
    param['a'] = param['a_end'] * (spsa['A'] + spsa['num_iter']) ** spsa['alpha']
    param['theta'] = param['start']
    params.append(param)
  return params


def validate_form(request):
  data = {
    'base_tag': request.POST['base-branch'],
    'new_tag': request.POST['test-branch'],
    'tc': request.POST['tc'],
    'book': request.POST['book'],
    'book_depth': request.POST['book-depth'],
    'base_signature': request.POST['base-signature'],
    'new_signature': request.POST['test-signature'],
    'base_options': request.POST['base-options'],
    'new_options': request.POST['new-options'],
    'username': authenticated_userid(request),
    'tests_repo': request.POST['tests-repo'],
    'info': request.POST['run-info'],
  }

  if not re.match('^([1-9]\d*/)?\d+(\.\d+)?(\+\d+(\.\d+)?)?$', data['tc']):
    raise Exception('Bad time control format')

  if request.POST.get('rescheduled_from'):
    data['rescheduled_from'] = request.POST['rescheduled_from']

  def strip_message(m):
    s = re.sub(r"[Bb]ench[ :]+[0-9]{7}\s*", "", m)
    s = re.sub(r"[ \t]+", " ", s)
    s = re.sub(r"\n+", r"\n", s)
    return s.rstrip()

  # Fill new_signature/info from commit info if left blank
  if len(data['new_signature']) == 0 or len(data['info']) == 0:
    api_url = data['tests_repo'].replace('https://github.com',
                                         'https://api.github.com/repos')
    api_url += ('/commits' + '/' + data['new_tag'])
    try:
      c = requests.get(api_url).json()
    except:
      raise Exception("Unable to access developer repository")
    if 'commit' not in c:
      raise Exception('Cannot find branch in developer repository')
    if len(data['new_signature']) == 0:
      bs = re.compile(r'(^|\s)[Bb]ench[ :]+([0-9]{7})', re.MULTILINE)
      m = bs.search(c['commit']['message'])
      if m:
        data['new_signature'] = m.group(2)
      else:
        raise Exception("This commit has no signature: please supply it manually.")
    if len(data['info']) == 0:
        data['info'] = ('' if re.match('^[012]?[0-9][^0-9].*', data['tc'])
                        else 'LTC: ') + strip_message(c['commit']['message'])

  # Check that the book exists in the official books repo
  if len(data['book']) > 0:
    api_url = 'https://api.github.com/repos/official-stockfish/books/contents'
    c = requests.get(api_url).json()
    matcher = re.compile(r'\.(epd|pgn)\.zip$')
    valid_book_filenames = [file['name'] for file in c if matcher.search(file['name'])]
    if data['book'] + '.zip' not in valid_book_filenames:
      raise Exception('Invalid book - ' + data['book'])

  if request.POST['stop_rule']=='spsa':
    data['base_signature']=data['new_signature']

  for k,v in data.items():
    if len(v)==0:
      raise Exception('Missing required option: %s' % k)

  data['auto_purge'] = request.POST.get('auto-purge') is not None

  # In case of reschedule use old data,
  # otherwise resolve sha and update user's tests_repo
  if 'resolved_base' in request.POST:
    data['resolved_base'] = request.POST['resolved_base']
    data['resolved_new'] = request.POST['resolved_new']
    data['msg_base'] = request.POST['msg_base']
    data['msg_new'] = request.POST['msg_new']
  else:
    data['resolved_base'], data['msg_base'] = get_sha(
        data['base_tag'], data['tests_repo'])
    data['resolved_new'], data['msg_new'] = get_sha(
        data['new_tag'], data['tests_repo'])
    u = request.userdb.get_user(data['username'])
    if u.get('tests_repo', '') != data['tests_repo']:
      u['tests_repo'] = data['tests_repo']
      request.userdb.users.save(u)

  if len(data['resolved_base']) == 0 or len(data['resolved_new']) == 0:
    raise Exception('Unable to find branch!')

  # Check entered bench
  if data['base_tag'] == 'master':
    found = False
    api_url = data['tests_repo'].replace('https://github.com',
                                         'https://api.github.com/repos')
    api_url += '/commits'
    bs = re.compile(r'(^|\s)[Bb]ench[ :]+([0-9]{7})', re.MULTILINE)
    for c in requests.get(api_url).json():
      m = bs.search(c['commit']['message'])
      if m:
        found = True
        break
    if not found or m.group(2) != data['base_signature']:
      raise Exception('Bench signature of Base master does not match, '
                      + 'please "git pull upstream master" !')

  stop_rule = request.POST['stop_rule']

  # Check if the base branch of the test repo matches official master
  api_url = 'https://api.github.com/repos/official-stockfish/Stockfish'
  api_url += '/compare/master...' + data['resolved_base'][:10]
  master_diff = requests.get(api_url, headers={
    'Accept': 'application/vnd.github.v3.diff'
  })
  data['base_same_as_master'] = master_diff.text is ''

  # Integer parameters

  if stop_rule == 'sprt':
    sprt_batch_size_games=8
    assert(sprt_batch_size_games%2==0)
    assert(request.rundb.chunk_size%sprt_batch_size_games==0)
    data['sprt'] = fishtest.stats.stat_util.SPRT(alpha=0.05,
                                                 beta=0.05,
                                                 elo0=float(request.POST['sprt_elo0']),
                                                 elo1=float(request.POST['sprt_elo1']),
                                                 elo_model='logistic',
                                                 batch_size=sprt_batch_size_games//2)  #game pairs
    # Limit on number of games played.
    # Shouldn't be hit in practice as long as it is larger than > ~200000
    # must scale with chunk_size to avoid overloading the server.
    data['num_games'] = 2000 * request.rundb.chunk_size
  elif stop_rule == 'spsa':
    data['num_games'] = int(request.POST['num-games'])
    if data['num_games'] <= 0:
      raise Exception('Number of games must be >= 0')

    data['spsa'] = {
      'A': int(request.POST['spsa_A']),
      'alpha': float(request.POST['spsa_alpha']),
      'gamma': float(request.POST['spsa_gamma']),
      'raw_params': request.POST['spsa_raw_params'],
      'iter': 0,
      'num_iter': int(data['num_games'] / 2),
      'clipping': request.POST['spsa_clipping'],
      'rounding': request.POST['spsa_rounding'],
    }
    data['spsa']['params'] = parse_spsa_params(
        request.POST['spsa_raw_params'], data['spsa'])
  else:
    data['num_games'] = int(request.POST['num-games'])
    if data['num_games'] <= 0:
      raise Exception('Number of games must be >= 0')

  max_games = 4000 * request.rundb.chunk_size
  if data['num_games'] > max_games:
    raise Exception('Number of games must be <= ' + str(max_games))

  data['threads'] = int(request.POST['threads'])
  data['priority'] = int(request.POST['priority'])
  data['throughput'] = int(request.POST['throughput'])

  if data['threads'] <= 0:
    raise Exception('Threads must be >= 1')

  return data


@view_config(route_name='tests_run', renderer='tests_run.mak', require_csrf=True)
def tests_run(request):
  if not authenticated_userid(request):
    request.session.flash('Please login')
    next_page = '/tests/run'
    if 'id' in request.params:
      next_page += '?id={}'.format(request.params['id'])
    return HTTPFound(location='{}?next={}'.format(request.route_url('login'), next_page))
  if request.method == 'POST':
    try:
      data = validate_form(request)
      run_id = request.rundb.new_run(**data)
      request.actiondb.new_run(authenticated_userid(request),
                               request.rundb.get_run(run_id))
      cached_flash(request, 'Submitted test to the queue!')
      return HTTPFound(location='/tests/view/' + str(run_id))
    except Exception as e:
      request.session.flash(str(e), 'error')

  run_args = {}
  if 'id' in request.params:
    run_args = request.rundb.get_run(request.params['id'])['args']

  username = authenticated_userid(request)
  u = request.userdb.get_user(username)

  return {'args': run_args,
          'is_rerun': len(run_args) > 0,
          'rescheduled_from': request.params['id'] if 'id' in request.params else None,
          'tests_repo': u.get('tests_repo', ''),
          'bench': get_master_bench()}


def can_modify_run(request, run):
  return (run['args']['username'] == authenticated_userid(request)
          or has_permission('approve_run', request.context, request))


@view_config(route_name='tests_modify', require_csrf=True, request_method='POST')
def tests_modify(request):
  if not authenticated_userid(request):
    request.session.flash('Please login')
    return HTTPFound(location=request.route_url('login'))
  if 'num-games' in request.POST:
    run = request.rundb.get_run(request.POST['run'])
    before = copy.deepcopy(run)

    if not can_modify_run(request, run):
      request.session.flash("Unable to modify another user's run!", 'error')
      return HTTPFound(location=request.route_url('tests'))

    existing_games = 0
    for chunk in run['tasks']:
      existing_games += chunk['num_games']
      if 'stats' in chunk:
        stats = chunk['stats']
        total = stats['wins'] + stats['losses'] + stats['draws']
        if total < chunk['num_games']:
          chunk['pending'] = True

    num_games = int(request.POST['num-games'])
    if (num_games > run['args']['num_games']
        and 'sprt' not in run['args']
        and 'spsa' not in run['args']):
      request.session.flash(
          'Unable to modify number of games in a fixed game test!', 'error')
      return HTTPFound(location=request.route_url('tests'))

    max_games = 4000 * request.rundb.chunk_size
    if num_games > max_games:
      request.session.flash('Number of games must be <= ' + str(max_games), 'error')
      return HTTPFound(location=request.route_url('tests'))

    if num_games > existing_games:
      # Create new chunks for the games
      new_chunks = request.rundb.generate_tasks(num_games - existing_games)
      run['tasks'] += new_chunks

    run['finished'] = False
    run['args']['num_games'] = num_games
    run['args']['priority'] = int(request.POST['priority'])
    run['args']['throughput'] = int(request.POST['throughput'])
    run['args']['auto_purge'] = True if request.POST.get('auto_purge') else False
    request.rundb.calc_itp(run)
    request.rundb.buffer(run, True)
    request.rundb.task_time = 0

    request.actiondb.modify_run(authenticated_userid(request), before, run)

    cached_flash(request, 'Run successfully modified!')
  return HTTPFound(location=request.route_url('tests'))


@view_config(route_name='tests_stop', require_csrf=True, request_method='POST')
def tests_stop(request):
  if not authenticated_userid(request):
    request.session.flash('Please login')
    return HTTPFound(location=request.route_url('login'))
  if 'run-id' in request.POST:
    run = request.rundb.get_run(request.POST['run-id'])
    if not can_modify_run(request, run):
      request.session.flash('Unable to modify another users run!', 'error')
      return HTTPFound(location=request.route_url('tests'))

    run['finished'] = True
    request.rundb.stop_run(request.POST['run-id'])
    request.actiondb.stop_run(authenticated_userid(request), run)
    cached_flash(request, 'Stopped run')
  return HTTPFound(location=request.route_url('tests'))


@view_config(route_name='tests_approve',
             require_csrf=True, request_method='POST')
def tests_approve(request):
  if not authenticated_userid(request):
    request.session.flash('Please login')
    return HTTPFound(location=request.route_url('login'))
  if not has_permission('approve_run', request.context, request):
    request.session.flash('Please login as approver')
    return HTTPFound(location=request.route_url('login'))
  username = authenticated_userid(request)
  run_id = request.POST['run-id']
  if request.rundb.approve_run(run_id, username):
    run = request.rundb.get_run(run_id)
    request.actiondb.approve_run(username, run)
    cached_flash(request, 'Approved run')
  else:
    request.session.flash('Unable to approve run!', 'error')
  return HTTPFound(location=request.route_url('tests'))


@view_config(route_name='tests_purge', require_csrf=True, request_method='POST')
def tests_purge(request):
  if not has_permission('approve_run', request.context, request):
    request.session.flash('Please login as approver')
    return HTTPFound(location=request.route_url('login'))
  username = authenticated_userid(request)

  run = request.rundb.get_run(request.POST['run-id'])
  if not run['finished']:
    request.session.flash('Can only purge completed run', 'error')
    return HTTPFound(location=request.route_url('tests'))

  purged = request.rundb.purge_run(run)
  if not purged:
    request.session.flash('No bad workers!')
    return HTTPFound(location=request.route_url('tests'))

  request.actiondb.purge_run(username, run)

  cached_flash(request, 'Purged run')
  return HTTPFound(location=request.route_url('tests'))


@view_config(route_name='tests_delete', require_csrf=True, request_method='POST')
def tests_delete(request):
  if not authenticated_userid(request):
    request.session.flash('Please login')
    return HTTPFound(location=request.route_url('login'))
  if 'run-id' in request.POST:
    run = request.rundb.get_run(request.POST['run-id'])
    if not can_modify_run(request, run):
      request.session.flash('Unable to modify another users run!', 'error')
      return HTTPFound(location=request.route_url('tests'))

    run['deleted'] = True
    run['finished'] = True
    for w in run['tasks']:
      w['pending'] = False
    request.rundb.buffer(run, True)
    request.rundb.task_time = 0

    request.actiondb.delete_run(authenticated_userid(request), run)

    cached_flash(request, 'Deleted run')
  return HTTPFound(location=request.route_url('tests'))


@view_config(route_name='tests_stats', renderer='tests_stats.mak')
def tests_stats(request):
  run = request.rundb.get_run(request.matchdict['id'])
  request.rundb.get_results(run)
  return {'run': run}


@view_config(route_name='tests_machines', renderer='machines_table.mak')
def tests_machines(request):
  machines = request.rundb.get_machines()
  for machine in machines:
    machine['last_updated'] = delta_date(machine['last_updated'])
  return {
    'machines': machines
  }


@view_config(route_name='tests_view_spsa_history', renderer='json')
def tests_view_spsa_history(request):
  run = request.rundb.get_run(request.matchdict['id'])
  if 'spsa' not in run['args']:
    return {}

  return run['args']['spsa']


@view_config(route_name='tests_view', renderer='tests_view.mak')
def tests_view(request):
  run = request.rundb.get_run(request.matchdict['id'])
  if run is None:
    raise exception_response(404)
  results = request.rundb.get_results(run)
  run['results_info'] = format_results(results, run)
  run_args = [('id', str(run['_id']), '')]
  if run.get('rescheduled_from'):
    run_args.append(('rescheduled_from', run['rescheduled_from'], ''))

  for name in ['new_tag', 'new_signature', 'new_options', 'resolved_new',
               'base_tag', 'base_signature', 'base_options', 'resolved_base',
               'sprt', 'num_games', 'spsa', 'tc', 'threads', 'book',
               'book_depth', 'auto_purge', 'priority', 'itp', 'username',
               'tests_repo', 'info']:

    if name not in run['args']:
      continue

    value = run['args'][name]
    url = ''

    if name == 'new_tag' and 'msg_new' in run['args']:
      value += '  (' + run['args']['msg_new'][:50] + ')'

    if name == 'base_tag' and 'msg_base' in run['args']:
      value += '  (' + run['args']['msg_base'][:50] + ')'

    if name == 'sprt' and value != '-':
      value = 'elo0: %.2f alpha: %.2f elo1: %.2f beta: %.2f state: %s (%s)' % \
              (value['elo0'], value['alpha'], value['elo1'], value['beta'],
               value.get('state', '-'), value.get('elo_model', 'BayesElo'))

    if name == 'spsa' and value != '-':
      iter_local = value['iter'] + 1  # assume at least one completed,
                                      # and avoid division by zero
      A = value['A']
      alpha = value['alpha']
      gamma = value['gamma']
      summary = 'Iter: %d, A: %d, alpha %0.3f, gamma %0.3f, clipping %s, rounding %s' \
              % (iter_local, A, alpha, gamma,
                 value['clipping'] if 'clipping' in value else 'old',
                 value['rounding'] if 'rounding' in value else 'deterministic')
      params = value['params']
      value = [summary]
      for p in params:
        value.append([
          p['name'],
          '{:.2f}'.format(p['theta']),
          int(p['start']),
          int(p['min']),
          int(p['max']),
          '{:.3f}'.format(p['c'] / (iter_local ** gamma)),
          '{:.3f}'.format(p['a'] / (A + iter_local) ** alpha)
        ])
    if 'tests_repo' in run['args']:
      if name == 'new_tag':
        url = run['args']['tests_repo'] + '/commit/' + run['args']['resolved_new']
      elif name == 'base_tag':
        url = run['args']['tests_repo'] + '/commit/' + run['args']['resolved_base']
      elif name == 'tests_repo':
        url = value

    if name == 'spsa':
      run_args.append(('spsa', value, ''))
    else:
      try:
        strval = str(value)
      except:
        strval = value.encode('ascii', 'replace')
      if name not in ['new_tag', 'base_tag']:
        strval = html.escape(strval)
      run_args.append((name, strval, url))

  active = 0
  cores = 0
  for task in run['tasks']:
    if task['active']:
      active += 1
      cores += task['worker_info']['concurrency']
    last_updated = task.get('last_updated', datetime.datetime.min)
    task['last_updated'] = last_updated

  if run['args'].get('sprt'):
    page_title = 'SPRT {} vs {}'.format(run['args']['new_tag'], run['args']['base_tag'])
  elif run['args'].get('spsa'):
    page_title = 'SPSA {}'.format(run['args']['new_tag'])
  else:
    page_title = '{} games - {} vs {}'.format(
      run['args']['num_games'],
      run['args']['new_tag'],
      run['args']['base_tag']
    )
  return {'run': run, 'run_args': run_args, 'page_title': page_title,
          'approver': has_permission('approve_run', request.context, request),
          'chi2': calculate_residuals(run),
          'totals': '(%s active worker%s with %s core%s)'
          % (active, ('s' if active != 1 else ''),
             cores, ('s' if cores != 1 else ''))}


def get_paginated_finished_runs(request):
  username = request.matchdict.get('username', '')
  success_only = request.params.get('success_only', False)
  yellow_only = request.params.get('yellow_only', False)
  ltc_only = request.params.get('ltc_only', False)

  page_idx = max(0, int(request.params.get('page', 1)) - 1)
  page_size = 50
  finished_runs, num_finished_runs = request.rundb.get_finished_runs(
    username=username, success_only=success_only,
    yellow_only=yellow_only, ltc_only=ltc_only,
    skip=page_idx * page_size, limit=page_size)

  pages = [{'idx': 'Prev', 'url': '?page={}'.format(page_idx),
            'state': 'disabled' if page_idx == 0 else ''}]
  for idx, _ in enumerate(range(0, num_finished_runs, page_size)):
    if idx < 5 or abs(page_idx - idx) < 5 or idx > (num_finished_runs / page_size) - 5:
      pages.append({'idx': idx + 1, 'url': '?page={}'.format(idx + 1),
                    'state': 'active' if page_idx == idx else ''})
    elif pages[-1]['idx'] != '...':
      pages.append({'idx': '...', 'url': '', 'state': 'disabled'})
  pages.append({'idx': 'Next', 'url': '?page={}'.format(page_idx + 2),
                'state': 'disabled' if page_idx + 1 == len(pages) - 1 else ''})

  for page in pages:
    if success_only:
      page['url'] += '&success_only=1'
    if yellow_only:
      page['url'] += '&yellow_only=1'
    if ltc_only:
      page['url'] += '&ltc_only=1'

  failed_runs = []
  for run in finished_runs:
    # Ensure finished runs have results_info
    results = request.rundb.get_results(run)
    if 'results_info' not in run:
      run['results_info'] = format_results(results, run)

    # Look for failed runs
    if results['wins'] + results['losses'] + results['draws'] == 0:
      failed_runs.append(run)

  return {
    'finished_runs': finished_runs,
    'finished_runs_pages': pages,
    'num_finished_runs': num_finished_runs,
    'failed_runs': failed_runs,
    'page_idx': page_idx,
  }


@view_config(route_name='tests_finished', renderer='tests_finished.mak')
def tests_finished(request):
  return get_paginated_finished_runs(request)


@view_config(route_name='tests_user', renderer='tests_user.mak')
def tests_user(request):
  username = request.matchdict.get('username', '')
  response = {
    **get_paginated_finished_runs(request),
    'username': username
  }
  if int(request.params.get('page', 1)) == 1:
    response['runs'] = request.rundb.aggregate_unfinished_runs(username)[0]
  # page 2 and beyond only show finished test results
  return response


def homepage_results(request):
  # Calculate games_per_minute from current machines
  games_per_minute = 0.0
  machines = request.rundb.get_machines()
  for machine in machines:
    machine['last_updated'] = delta_date(machine['last_updated'])
    if machine['nps'] != 0:
      games_per_minute += (
          (machine['nps'] / 1600000.0)
          * (60.0 / estimate_game_duration(machine['run']['args']['tc']))
          * (int(machine['concurrency']) // machine['run']['args'].get('threads', 1)))
  machines.reverse()
  # Get updated results for unfinished runs + finished runs
  (runs, pending_hours, cores, nps) = request.rundb.aggregate_unfinished_runs()
  return {
    **get_paginated_finished_runs(request),
    'runs': runs,
    'machines': machines,
    'pending_hours': '%.1f' % (pending_hours),
    'cores': cores,
    'nps': nps,
    'games_per_minute': int(games_per_minute),
  }


# For caching the homepage tests output
cache_time = 2
last_tests = None
last_time = 0

# Guard against parallel builds of main page
building = threading.Semaphore()

@view_config(route_name='tests', renderer='tests.mak')
def tests(request):
  if int(request.params.get('page', 1)) > 1:
    # page 2 and beyond only show finished test results
    return get_paginated_finished_runs(request)

  global last_tests, last_time
  if time.time() - last_time > cache_time:
    acquired = building.acquire(last_tests is None)
    if not acquired:
      # We have a current cache and another thread is rebuilding,
      # so return the current cache
      pass
    elif time.time() - last_time < cache_time:
      # Another thread has built the cache for us, so we are done
      building.release()
    else:
      # Not cached, so calculate and fetch homepage results
      try:
        last_tests = homepage_results(request)
      except Exception as e:
        print('Overview exception: ' + str(e))
        if not last_tests:
          raise e
      finally:
        last_time = time.time()
        building.release()
  return {
    **last_tests,
    'machines_shown': request.cookies.get('machines_state') == 'Hide',
    'pending_shown': request.cookies.get('pending_state') == 'Hide'
  }
