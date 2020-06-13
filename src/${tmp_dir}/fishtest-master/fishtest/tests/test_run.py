import unittest
import re

from fishtest.views import get_master_bench

class CreateRunTest(unittest.TestCase):

  def test_10_get_bench(self):
    self.assertTrue(re.match('[0-9]{7}|None', str(get_master_bench())))


if __name__ == "__main__":
  unittest.main()
