import unittest
import datetime

from pyramid import testing

from fishtest.views import login, signup
from fishtest.api import ApiView

import util

class Create10UsersTest(unittest.TestCase):

  def setUp(self):
    self.rundb = util.get_rundb()
    self.config = testing.setUp()
    self.config.add_route('login', '/login')
    self.config.add_route('signup', '/signup')

  def tearDown(self):
    self.rundb.userdb.users.delete_many({ 'username': 'JoeUser' })
    self.rundb.userdb.user_cache.delete_many({ 'username': 'JoeUser' })
    self.rundb.stop()
    testing.tearDown()

  def test_create_user(self):
    request = testing.DummyRequest(
      userdb=self.rundb.userdb,
      method='POST',
      remote_addr="127.0.0.1",
      params={
        'username': 'JoeUser',
        'password': 'secret',
        'password2': 'secret',
        'email': 'joe@user.net',
      }
    )
    response = signup(request)
    self.assertTrue('The resource was found at', response)


class Create50LoginTest(unittest.TestCase):

  def setUp(self):
    self.rundb = util.get_rundb()
    self.rundb.userdb.create_user('JoeUser', 'secret', 'email@email.email')
    self.config = testing.setUp()
    self.config.add_route('login', '/login')

  def tearDown(self):
    self.rundb.userdb.users.delete_many({ 'username': 'JoeUser' })
    self.rundb.userdb.user_cache.delete_many({ 'username': 'JoeUser' })
    self.rundb.stop()
    testing.tearDown()

  def test_login(self):
    request = testing.DummyRequest(
      userdb=self.rundb.userdb,
      method='POST',
      params = {
        'username': 'JoeUser',
        'password': 'badsecret'
      }
    )
    response = login(request)
    self.assertTrue('Invalid password' in request.session.pop_flash('error'))

    # Correct password, but still blocked from logging in
    request.params['password'] = 'secret'
    login(request)
    self.assertTrue('Blocked' in request.session.pop_flash('error'))

    # Unblock, then user can log in successfully
    user = self.rundb.userdb.get_user('JoeUser')
    user['blocked'] = False
    self.rundb.userdb.save_user(user)
    response = login(request)
    self.assertEqual(response.code, 302)
    self.assertTrue('The resource was found at' in str(response))


class Create90APITest(unittest.TestCase):
  def setUp(self):
    self.rundb = util.get_rundb()
    self.run_id = self.rundb.new_run('master', 'master', 100000,
                               '100+0.01', 'book', 10, 1, '', '',
                               username='travis', tests_repo='travis',
                               start_time=datetime.datetime.utcnow())
    self.rundb.userdb.user_cache.insert_one({
      'username': 'JoeUser',
      'cpu_hours': 12345
    })
    self.config = testing.setUp()
    self.config.add_route('api_stop_run', '/api/stop_run')

  def tearDown(self):
    self.rundb.userdb.users.delete_many({'username': 'JoeUser'})
    self.rundb.userdb.user_cache.delete_many({'username': 'JoeUser'})
    self.rundb.stop()
    testing.tearDown()

  def test_stop_run(self):
    request = testing.DummyRequest(
      rundb=self.rundb,
      userdb=self.rundb.userdb,
      actiondb=self.rundb.actiondb,
      method='POST',
      json_body={
        'username': 'JoeUser',
        'password': 'secret',
        'run_id': self.run_id,
        'message': 'travis'
      }
    )
    response = ApiView(request).stop_run()
    self.assertEqual(response, {})
    run = request.rundb.get_run(request.json_body['run_id'])
    self.assertEqual(run['stop_reason'], 'travis')


if __name__ == "__main__":
  unittest.main()
