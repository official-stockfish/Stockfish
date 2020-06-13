from __future__ import absolute_import

import datetime
import glob
import os
import shutil
import sys
import requests
import time
from zipfile import ZipFile
from distutils.dir_util import copy_tree

start_dir = os.getcwd()

WORKER_URL = 'https://github.com/glinscott/fishtest/archive/master.zip'

def do_restart():
  """Restarts the worker, using the same arguments"""
  args = sys.argv[:]
  args.insert(0, sys.executable)
  if sys.platform == 'win32':
    args = ['"%s"' % arg for arg in args]

  os.chdir(start_dir)
  os.execv(sys.executable, args) # This does not return !

def update(restart=True, test=False):
  worker_dir = os.path.dirname(os.path.realpath(__file__))
  update_dir = os.path.join(worker_dir, 'update')
  if not os.path.exists(update_dir):
    os.makedirs(update_dir)

  worker_zip = os.path.join(update_dir, 'wk.zip')
  with open(worker_zip, 'wb+') as f:
    f.write(requests.get(WORKER_URL).content)

  zip_file = ZipFile(worker_zip)
  zip_file.extractall(update_dir)
  zip_file.close()
  prefix = os.path.commonprefix([n.filename for n in zip_file.infolist()])
  worker_src = os.path.join(update_dir, prefix + 'worker')
  if not test:
    copy_tree(worker_src, worker_dir)
  else:
    file_list = os.listdir(worker_src)
  shutil.rmtree(update_dir)

  # rename the testing_dir as backup
  # and to trigger the download of update files
  testing_dir = os.path.join(worker_dir, 'testing')
  if os.path.exists(testing_dir):
    try:
      time_stamp = str(datetime.datetime.timestamp(datetime.datetime.utcnow()))
    except AttributeError: # python2
      dt_utc = datetime.datetime.utcnow()
      time_stamp = str(time.mktime(dt_utc.timetuple()) + dt_utc.microsecond/1e6)

    bkp_testing_dir = os.path.join(worker_dir, '_testing_' + time_stamp)
    shutil.move(testing_dir, bkp_testing_dir)
    os.makedirs(testing_dir)
    # delete the old engine binaries
    engines = glob.glob(os.path.join(bkp_testing_dir, 'stockfish_*'))
    for engine in engines:
      try:
        os.remove(engine)
      except:
        print('Note: failed to delete an engine binary ' + str(engine))
        pass
    # clean up old folder backups (keeping the num_bkps most recent)
    bkp_dirs = glob.glob(os.path.join(worker_dir, '_testing_*'))
    num_bkps = 3
    if len(bkp_dirs) > num_bkps:
      bkp_dirs.sort(key=os.path.getmtime)
      for old_bkp_dir in bkp_dirs[:-num_bkps]:
        try:
          shutil.rmtree(old_bkp_dir)
        except:
          print('Note: failed to remove the old backup folder ' + str(old_bkp_dir))
          pass

  print("start_dir: " + start_dir)
  if restart:
    do_restart()

  if test:
    return file_list

if __name__ == '__main__':
  update(False)
