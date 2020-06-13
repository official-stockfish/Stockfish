import os
import sys

from pyramid.authentication import AuthTktAuthenticationPolicy
from pyramid.authorization import ACLAuthorizationPolicy
from pyramid.config import Configurator
from pyramid.events import NewRequest, BeforeRender
from pyramid.session import UnencryptedCookieSessionFactoryConfig

from fishtest.rundb import RunDb
from fishtest import helpers

def main(global_config, **settings):
  """ This function returns a Pyramid WSGI application.
  """
  session_factory = UnencryptedCookieSessionFactoryConfig('fishtest')
  config = Configurator(settings=settings,
                        session_factory=session_factory,
                        root_factory='fishtest.models.RootFactory')
  config.include('pyramid_mako')
  config.set_default_csrf_options(require_csrf=False)

  rundb = RunDb()

  def add_rundb(event):
    event.request.rundb = rundb
    event.request.userdb = rundb.userdb
    event.request.actiondb = rundb.actiondb

  def add_renderer_globals(event):
    event['h'] = helpers

  config.add_subscriber(add_rundb, NewRequest)
  config.add_subscriber(add_renderer_globals, BeforeRender)

  # Authentication
  def group_finder(username, request):
    return request.userdb.get_user_groups(username)

  with open(os.path.expanduser('~/fishtest.secret'), 'r') as f:
    secret = f.read()
  config.set_authentication_policy(
    AuthTktAuthenticationPolicy(
      secret, callback=group_finder, hashalg='sha512', http_only=True))
  config.set_authorization_policy(ACLAuthorizationPolicy())

  config.add_static_view('html', 'static/html', cache_max_age=3600)
  config.add_static_view('css', 'static/css', cache_max_age=3600)
  config.add_static_view('js', 'static/js', cache_max_age=3600)
  config.add_static_view('img', 'static/img', cache_max_age=3600)

  config.add_route('home', '/')
  config.add_route('login', '/login')
  config.add_route('logout', '/logout')
  config.add_route('signup', '/signup')
  config.add_route('user', '/user/{username}')
  config.add_route('profile', '/user')
  config.add_route('pending', '/pending')
  config.add_route('users', '/users')
  config.add_route('users_monthly', '/users/monthly')
  config.add_route('actions', '/actions')

  config.add_route('tests', '/tests')
  config.add_route('tests_machines', '/tests/machines')
  config.add_route('tests_finished', '/tests/finished')
  config.add_route('tests_run', '/tests/run')
  config.add_route('tests_view', '/tests/view/{id}')
  config.add_route('tests_view_spsa_history', '/tests/view/{id}/spsa_history')
  config.add_route('tests_user', '/tests/user/{username}')
  config.add_route('tests_stats', '/tests/stats/{id}')

  # Tests - actions
  config.add_route('tests_modify', '/tests/modify')
  config.add_route('tests_delete', '/tests/delete')
  config.add_route('tests_stop', '/tests/stop')
  config.add_route('tests_approve', '/tests/approve')
  config.add_route('tests_purge', '/tests/purge')

  # API
  config.add_route('api_request_task', '/api/request_task')
  config.add_route('api_update_task', '/api/update_task')
  config.add_route('api_failed_task', '/api/failed_task')
  config.add_route('api_stop_run', '/api/stop_run')
  config.add_route('api_request_version', '/api/request_version')
  config.add_route('api_request_spsa', '/api/request_spsa')
  config.add_route('api_active_runs', '/api/active_runs')
  config.add_route('api_get_run', '/api/get_run/{id}')
  config.add_route('api_upload_pgn', '/api/upload_pgn')
  config.add_route('api_download_pgn', '/api/pgn/{id}')
  config.add_route('api_download_pgn_100', '/api/pgn_100/{skip}')
  config.add_route('api_get_elo', '/api/get_elo/{id}')

  config.scan()
  return config.make_wsgi_app()
