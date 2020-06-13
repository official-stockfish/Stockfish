import threading
import smtplib
from datetime import datetime
from collections import defaultdict
from email.mime.text import MIMEText

import numpy
import scipy.stats

import fishtest.stats.stat_util

UUID_MAP = defaultdict(dict)
key_lock = threading.Lock()

FISH_URL = 'https://tests.stockfishchess.org/tests/view/'


def get_worker_key(task):
  global UUID_MAP

  if 'worker_info' not in task:
    return '-'
  username = task['worker_info'].get('username', '')
  cores = str(task['worker_info']['concurrency'])

  uuid = task['worker_info'].get('unique_key', '')
  with key_lock:
    if uuid not in UUID_MAP[username]:
      next_idx = len(UUID_MAP[username])
      UUID_MAP[username][uuid] = next_idx

  worker_key = '%s-%scores' % (username, cores)
  suffix = UUID_MAP[username][uuid]
  if suffix != 0:
    worker_key += "-" + str(suffix)

  return worker_key


def get_chi2(tasks, bad_users):
  """ Perform chi^2 test on the stats from each worker """
  results = {'chi2': 0.0, 'dof': 0, 'p': 0.0, 'residual': {}}

  # Aggregate results by worker
  users = {}
  for task in tasks:
    task['worker_key'] = get_worker_key(task)
    if 'worker_info' not in task:
      continue
    key = get_worker_key(task)
    if key in bad_users:
      continue
    stats = task.get('stats', {})
    wld = [float(stats.get('wins', 0)),
           float(stats.get('losses', 0)),
           float(stats.get('draws', 0))]
    if wld == [0.0, 0.0, 0.0]:
      continue
    if key in users:
      for idx in range(len(wld)):
        users[key][idx] += wld[idx]
    else:
      users[key] = wld

  if len(users) == 0:
    return results

  observed = numpy.array(list(users.values()))
  rows, columns = observed.shape
  # Results only from one worker: skip the test for workers homogeneity
  if rows == 1:
    return {
      'chi2': float('nan'),
      'dof': 0,
      'p': float('nan'),
      'residual': {}
    }
  column_sums = numpy.sum(observed, axis=0)
  columns_not_zero = sum(i > 0 for i in column_sums)
  df = (rows - 1) * (columns - 1)

  if columns_not_zero == 0:
    return results
  # Results only of one type: workers are identical wrt the test
  elif columns_not_zero == 1:
    results = {'chi2': 0.0, 'dof': df, 'p': 1.0, 'residual': {}}
    return results
  # Results only of two types: workers are identical wrt the missing result type
  # Change the data shape to avoid divide by zero
  elif columns_not_zero == 2:
    idx = numpy.argwhere(numpy.all(observed[..., :] == 0, axis=0))
    observed = numpy.delete(observed, idx, axis=1)
    column_sums = numpy.sum(observed, axis=0)

  row_sums = numpy.sum(observed, axis=1)
  grand_total = numpy.sum(column_sums)

  expected = numpy.outer(row_sums, column_sums) / grand_total
  raw_residual = observed - expected
  std_error = numpy.sqrt(expected *
                         numpy.outer((1 - row_sums / grand_total),
                                     (1 - column_sums / grand_total)))
  adj_residual = raw_residual / std_error
  for idx in range(len(users)):
    users[list(users.keys())[idx]] = numpy.max(numpy.abs(adj_residual[idx]))
  chi2 = numpy.sum(raw_residual * raw_residual / expected)
  return {
    'chi2': chi2,
    'dof': df,
    'p': 1 - scipy.stats.chi2.cdf(chi2, df),
    'residual': users,
  }


def calculate_residuals(run):
  bad_users = set()
  chi2 = get_chi2(run['tasks'], bad_users)
  residuals = chi2['residual']

  # Limit bad users to 1 for now
  for _ in range(1):
    worst_user = {}
    for task in run['tasks']:
      if task['worker_key'] in bad_users:
        continue
      task['residual'] = residuals.get(task['worker_key'], 0.0)

      # Special case crashes or time losses
      stats = task.get('stats', {})
      crashes = stats.get('crashes', 0)
      if crashes > 3:
        task['residual'] = 8.0

      if abs(task['residual']) < 2.0:
        task['residual_color'] = '#44EB44'
      elif abs(task['residual']) < 2.7:
        task['residual_color'] = 'yellow'
      else:
        task['residual_color'] = '#FF6A6A'

      if chi2['p'] < 0.001 or task['residual'] > 7.0:
        if len(worst_user) == 0 or task['residual'] > worst_user['residual']:
          worst_user['worker_key'] = task['worker_key']
          worst_user['residual'] = task['residual']

    if len(worst_user) == 0:
      break
    bad_users.add(worst_user['worker_key'])
    residuals = get_chi2(run['tasks'], bad_users)['residual']

  chi2['bad_users'] = bad_users
  return chi2


def format_results(run_results, run):
  result = {'style': '', 'info': []}

  # win/loss/draw count
  WLD = [run_results['wins'], run_results['losses'], run_results['draws']]

  if 'spsa' in run['args']:
    result['info'].append('%d/%d iterations'
                          % (run['args']['spsa']['iter'],
                             run['args']['spsa']['num_iter']))
    result['info'].append('%d/%d games played'
                          % (WLD[0] + WLD[1] + WLD[2],
                             run['args']['num_games']))
    return result

  # If the score is 0% or 100% the formulas will crash
  # anyway the statistics are only asymptotic
  if WLD[0] == 0 or WLD[1] == 0:
    result['info'].append('Pending...')
    return result

  state = 'unknown'
  if 'sprt' in run['args']:
    sprt = run['args']['sprt']
    state = sprt.get('state', '')
    elo_model = sprt.get('elo_model', 'BayesElo')
    if not 'llr' in sprt:  # legacy
      fishtest.stats.stat_util.update_SPRT(run_results,sprt)
    if elo_model == 'BayesElo':
      result['info'].append('LLR: %.2f (%.2lf,%.2lf) [%.2f,%.2f]'
                            % (sprt['llr'],
                               sprt['lower_bound'], sprt['upper_bound'],
                               sprt['elo0'], sprt['elo1']))
    else:
      result['info'].append('LLR: %.2f (%.2lf,%.2lf) {%.2f,%.2f}'
                            % (sprt['llr'],
                               sprt['lower_bound'], sprt['upper_bound'],
                               sprt['elo0'], sprt['elo1']))
  else:
    if 'pentanomial' in run_results.keys():
      elo, elo95, los = fishtest.stats.stat_util.get_elo(
          run_results['pentanomial'])
    else:
      elo, elo95, los = fishtest.stats.stat_util.get_elo(
          [WLD[1], WLD[2], WLD[0]])

    # Display the results
    eloInfo = 'ELO: %.2f +-%.1f (95%%)' % (elo, elo95)
    losInfo = 'LOS: %.1f%%' % (los * 100)

    result['info'].append(eloInfo + ' ' + losInfo)

    if los < 0.05:
      state = 'rejected'
    elif los > 0.95:
      state = 'accepted'

  result['info'].append('Total: %d W: %d L: %d D: %d'
                        % (sum(WLD), WLD[0], WLD[1], WLD[2]))
  if 'pentanomial' in run_results.keys():
    result['info'].append("Ptnml(0-2): " + ", ".join(
        str(run_results['pentanomial'][i]) for i in range(0, 5)))

  if state == 'rejected':
    if WLD[0] > WLD[1]:
      result['style'] = 'yellow'
    else:
      result['style'] = '#FF6A6A'
  elif state == 'accepted':
    if ('sprt' in run['args']
        and (float(sprt['elo0']) + float(sprt['elo1'])) < 0.0):
      result['style'] = '#66CCFF'
    else:
      result['style'] = '#44EB44'
  return result



def estimate_game_duration(tc):
  # Total time for a game is assumed to be the double of tc for each player
  # reduced for 92% because on average a game is stopped earlier (LTC fishtest result).
  scale = 2 * 0.92
  # estimated number of moves per game (LTC fishtest result)
  game_moves = 68

  chunks = tc.split('+')
  increment = 0.0
  if len(chunks) == 2:
    increment = float(chunks[1])

  chunks = chunks[0].split('/')
  num_moves = 0
  if len(chunks) == 2:
    num_moves = int(chunks[0])

  time_tc = chunks[-1]
  chunks = time_tc.split(':')
  if len(chunks) == 2:
    time_tc = float(chunks[0]) * 60 + float(chunks[1])
  else:
    time_tc = float(chunks[0])

  if num_moves > 0:
    time_tc = time_tc * (game_moves / num_moves)

  return (time_tc + (increment * game_moves)) * scale


def remaining_hours(run):
  r = run['results']
  if 'sprt' in run['args']:
    # current average number of games. Regularly update / have server guess?
    expected_games = 53000
    # checking randomly, half the expected games needs still to be done
    remaining_games = expected_games / 2
  else:
    expected_games = run['args']['num_games']
    remaining_games = max(0,
                          expected_games
                          - r['wins'] - r['losses'] - r['draws'])
  game_secs = estimate_game_duration(run['args']['tc'])
  return game_secs * remaining_games * int(
      run['args'].get('threads', 1)) / (60*60)


def post_in_fishcooking_results(run):
  """ Posts the results of the run to the fishcooking forum:
      https://groups.google.com/forum/?fromgroups=#!forum/fishcooking
  """
  title = run['args']['new_tag'][:23]

  if 'username' in run['args']:
    title += '  (' + run['args']['username'] + ')'

  body = FISH_URL + '%s\n\n' % (str(run['_id']))

  body += run['start_time'].strftime("%d-%m-%y") + ' from '
  body += run['args'].get('username', '') + '\n\n'

  body += run['args']['new_tag'] + ': ' + run['args'].get(
      'msg_new', '') + '\n'
  body += run['args']['base_tag'] + ': ' + run['args'].get(
      'msg_base', '') + '\n\n'

  body += 'TC: ' + run['args']['tc'] + ' th ' + str(
      run['args'].get('threads', 1)) + '\n'
  body += '\n'.join(run['results_info']['info']) + '\n\n'

  body += run['args'].get('info', '') + '\n\n'

  msg = MIMEText(body)
  msg['Subject'] = title
  msg['From'] = 'fishtest@noreply.github.com'
  msg['To'] = 'fishcooking_results@googlegroups.com'

  try:
    s = smtplib.SMTP('localhost')
    s.sendmail(msg['From'], [msg['To']], msg.as_string())
    s.quit()
  except ConnectionRefusedError:
    print('Unable to post results to fishcooking forum')


def delta_date(date):
  if date != datetime.min:
    diff = datetime.utcnow() - date
    if diff.days != 0:
      delta = '%d days ago' % (diff.days)
    elif diff.seconds / 3600 > 1:
      delta = '%d hours ago' % (diff.seconds / 3600)
    elif diff.seconds / 60 > 1:
      delta = '%d minutes ago' % (diff.seconds / 60)
    else:
      delta = 'seconds ago'
  else:
    delta = 'Never'
  return delta
