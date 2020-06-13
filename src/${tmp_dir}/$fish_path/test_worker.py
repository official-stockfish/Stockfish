import unittest
import worker
import updater
import os
import os.path
import subprocess
import games

class workerTest(unittest.TestCase):

  def tearDown(self):
    if os.path.exists('foo.txt'):
      os.remove('foo.txt')
    if os.path.exists('README.md'):
      os.remove('README.md')

  def test_item_download(self):
    try:
      games.setup('README.md', '.')
      self.assertTrue(os.path.exists(os.path.join('.','README.md')))
    except KeyError:
      pass

  def test_config_setup(self):
    config = worker.setup_config_file('foo.txt')

    self.assertTrue(config.has_section('login'))
    self.assertTrue(config.has_section('parameters'))
    self.assertTrue(config.has_option('login', 'username'))
    self.assertTrue(config.has_option('login', 'password'))
    self.assertTrue(config.has_option('parameters', 'host'))
    self.assertTrue(config.has_option('parameters', 'port'))
    self.assertTrue(config.has_option('parameters', 'concurrency'))

  def test_worker_script(self):
    p = subprocess.Popen(["python" , "worker.py"], stderr = subprocess.PIPE)
    result = p.stderr.readline()
    if not isinstance(result, str):
      result = result.decode('utf-8')
    self.assertEqual(result, 'worker.py [username] [password]\n')
    p.stderr.close()

  def test_setup_exception(self):
    cwd = os.getcwd()
    with self.assertRaises(Exception):
      games.setup_engine('foo', cwd, 'foo', 'https://foo', 1)

  def test_updater(self):
    file_list = updater.update(restart=False, test=True)
    print(file_list)
    self.assertTrue('worker.py' in file_list)

if __name__ == "__main__":
  unittest.main()
