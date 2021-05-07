#!/usr/bin/env python3

# For usage example use:
#   python3 ./make_release.py --help

import subprocess
import threading
import os
import sys
import re
import shutil
import uuid
import stat
import concurrent.futures
import argparse
import logging
import platform
import traceback
from zipfile import ZipFile

def get_logger():
    logger = logging.getLogger('make_release')
    logger.setLevel(logging.DEBUG)
    handler = logging.StreamHandler()
    formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s', '%H:%M:%S')
    handler.setFormatter(formatter)
    logger.addHandler(handler)
    return logger

parser = argparse.ArgumentParser(description='Stockfish release maker', formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--sde', default=None, type=str,
                    help='Absolute path to Intel SDE application. If not specified it will not be used. If specified it will be used as a fallback for architectures that fail to compile.')
parser.add_argument('--concurrency', default=1, type=int,
                    help='The maximum number of jobs to run concurrently.')
parser.add_argument('--repository', default='https://github.com/official-stockfish/Stockfish.git', type=str,
                    help='The path to the repository to clone.')
parser.add_argument('--clone-dir', default='cloned', type=str,
                    help='The directory to clone the repository to.')
parser.add_argument('--out-dir', default='out', type=str,
                    help='The directory that shall contain the resulting binaries.')
parser.add_argument('--build-type', default='profile-build', type=str,
                    help='The build type to use in make.')
parser.add_argument('--prefix', default='', type=str,
                    help='The string put in the binary name after stockfish- but before -arch. For example "win" or "linux". Will be ommited if empty.')
parser.add_argument('--verbose', default=False, action='store_true',
                    help='Keep output of running programs.')
parser.add_argument('--keep-logs', default=False, action='store_true',
                    help='Whether to keep logs in the output directory. Incompatible with --verbose.')
parser.add_argument('--bench', default=None, type=int,
                    help='If specified the bench of resulting binaries will be verified and must match this value.')
parser.add_argument('--max-num-workers', default=None, type=int,
                    help='Sometimes too many compilation processes running in parallel can cause issues. If this is specified it will limit the number of parallel arch builds to this number. Remaining threads specified by concurrency will be used for make jobs.')
parser.add_argument('--compiler', default=None, type=str,
                    help='If specified this will be passed as COMP to the make script.')
parser.add_argument('--clone-depth', default=25, type=int,
                    help='The number of commits to clone history of. For retrieving bench more than one might be needed. Default is setup such that it should work for all normal cases.')
parser.add_argument('--max-tries', default=1, type=int,
                    help='How many times a build for a single target can be attempted. Builds fail sometimes for weird reasons.')
parser.add_argument('archs', nargs='*', default=None, type=str,
                    help='A list of architectures to compile. If not provided then most sensible archs will be built.')

args = parser.parse_args()

if args.verbose and args.keep_logs:
    parser.error('--verbose and --keep-logs are currently not compatible.')

SDE_PATH = args.sde
NUM_THREADS = args.concurrency
CLONE_DIR = args.clone_dir
CLONE_DEPTH = args.clone_depth
OUT_DIR = args.out_dir
REPO = args.repository
BUILD_TYPE = args.build_type
ARCHS = args.archs
STDOUT = None if args.verbose else subprocess.DEVNULL
STDERR = subprocess.STDOUT
PREFIX = args.prefix
IS_WINDOWS = 'windows' in platform.system().lower()
EXE_SUFFIX = '.exe' if IS_WINDOWS else ''
ZIP_SUFFIX = '.zip'
EXPECTED_BENCH = args.bench
MAX_NUM_WORKERS = NUM_THREADS if args.max_num_workers is None else args.max_num_workers
MAX_TRIES = args.max_tries
COMPILER = args.compiler
LOGGER = get_logger()

def remove_readonly(func, path, _):
    '''
    Clear the readonly bit and reattempt the removal
    '''
    os.chmod(path, stat.S_IWRITE)
    func(path)

def clone_stockfish(destination_dir, repository, branch='master', clone_depth=1):
    '''
    Clone, with depth 1, the specified `branch` from the `repository`.
    The repository is cloned to the `destination_dir`.
    '''
    LOGGER.info('Cloning branch {} from repository {} into {}' \
                .format(branch, repository, destination_dir))

    command = ['git', 'clone', '--branch', branch, '--depth', str(clone_depth), repository, destination_dir]
    LOGGER.info('Cloning command is: {}'.format(' '.join(command)))

    subprocess.run( \
        command, \
        encoding='utf-8', stdout=STDOUT, stderr=STDERR)

    LOGGER.info('Cloning finished.')

def download_net(repo_dir):
    LOGGER.info('Executing `make net`.')
    ret = subprocess.run(['make', 'net'], cwd=os.path.join(repo_dir, 'src'))
    if ret.returncode != 0:
        LOGGER.error('Failed to download the net.')
        raise Exception('Failed to download the net')
    else:
        LOGGER.info('Sucessfully downloaded the net.')

class DummyContextManager():
    '''
    Useful for "conditional" `with` statements.
    '''

    def __enter__(self):
        return None

    def __exit__(self, exc_type, exc_value, traceback):
        return False

class TemporaryDirectory():
    '''
    Maintains a temporary directory with a unique name.
    '''

    def __init__(self, prefix='', add_uuid=True):
        self.path = prefix
        if add_uuid:
            self.path += str(uuid.uuid4())
        os.mkdir(self.path)
        LOGGER.info('Created temporary directory {}'.format(self.path))

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        shutil.rmtree(self.path, onerror=remove_readonly)
        LOGGER.info('Removed temporary directory {}'.format(self.path))

def get_bench_from_git_history(repo_dir):
    command = ['git', '--git-dir', os.path.join(repo_dir, '.git'), 'log']
    completed_process = subprocess.run(command, encoding='utf-8', stdout=subprocess.PIPE, universal_newlines=True)
    output = completed_process.stdout
    try:
        m = re.search('[Bb]ench[ :]+(\\d*)', output)
        return int(m.group(1))
    except:
        return None

def pack_binary_with_source(dest_dir, repo_dir, src_file, dest_filename):
    '''
    Takes the src_file (expected to be stockfish's binary) and
    the `repo_dir` and produces a .zip archive with them both.
    The archive is put into `dest_dir` and has the following structure:
    `artifact_name` is the stem of `dest_filename`
    - `dest_dir`
        - `artifact_name`+ZIP_SUFFIX
            - `artifact_name`
                - `Stockfish`
                    - `src`
                    - rest of the repo, without .git
                - `artifact_name`+EXE_SUFFIX
    '''

    artifact_name = os.path.splitext(dest_filename)[0]
    archive_filename = artifact_name + ZIP_SUFFIX
    archive_path = os.path.join(dest_dir, archive_filename)

    LOGGER.info('Creating archive {}.'.format(archive_path))

    with ZipFile(archive_path, 'w') as archive:
        # Write the stockfish binary, AUTHORS, Top CPU Contributors.txt,
        # and Copying.txt files to the main directory.
        main_level_files = [
            (src_file, dest_filename),
            (os.path.join(repo_dir, 'AUTHORS'), 'AUTHORS'),
            (os.path.join(repo_dir, 'Copying.txt'), 'Copying.txt'),
            (os.path.join(repo_dir, 'Top CPU Contributors.txt'), 'Top CPU Contributors.txt')
        ]
        for path_from, name in main_level_files:
            archive.write(path_from, os.path.join(artifact_name, name))

        # Write the source directory, excluding .git and *.nnue.
        for path, subdirs, files in os.walk(repo_dir, followlinks=False):
            if '.git' in path:
                continue

            for filename in files:
                if filename.endswith('.nnue'):
                    continue

                full_path = os.path.join(path, filename)
                rel_path = os.path.relpath(full_path, repo_dir)
                internal_path = os.path.join(artifact_name, 'Stockfish', rel_path)

                archive.write(full_path, internal_path)

    success = True
    with ZipFile(archive_path, 'r') as archive:
        if archive.testzip() is not None:
            success = False

    if not success:
        os.remove(archive_path)
        raise Exception('The CRC check for {} failed.'.format(archive_path))

    LOGGER.info('Created archive {}.'.format(archive_path))

def run_bench(stockfish):
    command = [stockfish, 'bench']
    if SDE_PATH:
        command = [SDE_PATH, '--'] + command

    proc = subprocess.run(command, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, universal_newlines=True)
    return int([line for line in proc.stderr.splitlines() if 'Nodes searched' in line][0].split(':')[1].strip())

def compile_target(
    *,
    repo_dir,
    out_dir,
    build_type='profile-build',
    arch='x86-64-modern',
    prefix='',
    expected_bench=None,
    num_threads=1,
    compiler=None):
    '''
    This function performs the following:
    1. Copies the contents of `repo_dir` into a temporary directory.
    2. Runs `make clean` in that temporary directory + ./src/
    3. Runs `make `build_type`` for the specified `arch`.
       NOTE: If SDE_PATH is not empty then SDE_PATH is set for all make commands.
    4. Copies the resulting executable file into `out_dir` with a given `prefix`
    '''
    assert num_threads >= 1

    if prefix != '':
        prefix = '-' + prefix

    if args.keep_logs:
        # If the user wants to keep logs a file is created and stdout redirected to it.
        # Otherwise we use the dummy context manager and stdout is not redirected.
        logfile_name = os.path.join(out_dir, 'stockfish{}-{}-{}.log'.format(prefix, build_type, arch))
        LOGGER.info('{}: Creating log file at {}.'.format(arch, logfile_name))

        logfile = open(logfile_name, 'w')
        stdout = logfile
    else:
        logfile = DummyContextManager()
        stdout = STDOUT

    with logfile:
        with TemporaryDirectory('./stockfish-{}-{}-'.format(build_type, arch)) as copy_dir:
            src_dir = os.path.join(copy_dir.path, 'src')

            LOGGER.info('{}: Copying original files from {} to {}'.format(arch, repo_dir, copy_dir.path))

            shutil.copytree(os.path.join(repo_dir, 'src'), os.path.join(copy_dir.path, 'src'))

            LOGGER.info('{}: Running `make clean`'.format(arch))

            subprocess.run( \
                ['make', 'clean'], \
                cwd=src_dir, stdout=stdout, stderr=STDERR)

            # We form the make command used to build stockfish.
            # If SDE_PATH is given the only thing that needs to be done is
            # it needs to be passed to the make script.
            # There is no downside to always running under SDE if provided.
            make_command = ['make', build_type, 'ARCH={}'.format(arch)]
            if SDE_PATH:
                make_command += ['SDE_PATH="{}"'.format(SDE_PATH)]
            if compiler:
                make_command += ['COMP={}'.format(compiler)]
            if num_threads > 1:
                make_command += ['-j{}'.format(num_threads)]

            make_command_log = ' '.join(make_command)
            LOGGER.info('{}: Running `{}`' \
                .format(arch, make_command_log))

            ret = subprocess.run(make_command, cwd=src_dir, stdout=stdout, stderr=STDERR)

            # This will also detect profile-builds that failed due to illegal instructions
            # but it requires user input on windows (closing the error dialog)
            # unless ran under msys or equivalent.
            # A proper solution would query the ISA, for example using gcc, but
            # normally for such cases SDE_PATH will be specified.
            if ret.returncode != 0:
                LOGGER.error('{}: Error running `{}`. Exit code: {}' \
                            .format(arch, make_command_log, ret.returncode))
                raise Exception('Make script failed')
            else:
                src_file = os.path.join(src_dir, 'stockfish' + EXE_SUFFIX)

                LOGGER.info('{}: Finished build of {}.'.format(arch, src_file))
                LOGGER.info('{}: Running `make strip`.'.format(arch))

                subprocess.run(['make', 'strip'], cwd=src_dir, stdout=stdout, stderr=STDERR)

                LOGGER.info('{}: Verifying bench.'.format(arch))

                bench = run_bench(src_file)
                if bench == expected_bench:
                    LOGGER.info('{}: Correct bench: {}'.format(arch, bench))
                    LOGGER.info('{}: Producing the release archive.'.format(arch))
                    dest_filename = 'stockfish{}-{}{}'.format(prefix, arch, EXE_SUFFIX)
                    pack_binary_with_source(out_dir, repo_dir, src_file, dest_filename)
                else:
                    LOGGER.error('{}: Invalid bench. Expected {}, got {}.'.format(arch, expected_bench, bench))
                    raise Exception('Invalid bench')

def get_possible_targets(repo_dir):
    '''
    Parse the output of `make help` and extract the available targets
    '''
    p = subprocess.run(['make', 'help'], cwd=os.path.join(repo_dir, 'src'),
                       encoding='utf-8', stdout=subprocess.PIPE, universal_newlines=True)

    targets = []
    read_targets = False

    for line in p.stdout.splitlines():
        if 'Supported compilers:' in line:
            read_targets = False
        if read_targets and len(line.split()) > 1:
            targets.append(line.split()[0])
        if 'Supported archs:' in line:
            read_targets = True

    targets = [target for target in targets if "apple" not in target if "arm" not in target if "ppc" not in target]

    return targets

def main():
    if os.path.exists(CLONE_DIR):
        # We can't rely on ignoring errors for this, because
        # then we also ignore write protection errors...
        # So we have to take this race condition.
        shutil.rmtree(CLONE_DIR, onerror=remove_readonly)

    with TemporaryDirectory(CLONE_DIR, False):
        try:
            os.mkdir(OUT_DIR)
        except FileExistsError as e:
            LOGGER.warning('Directory {} already exists.'.format(OUT_DIR))

        clone_stockfish(CLONE_DIR, REPO, clone_depth=CLONE_DEPTH)
        download_net(CLONE_DIR)

        expected_bench = EXPECTED_BENCH if EXPECTED_BENCH is not None else get_bench_from_git_history(CLONE_DIR)
        if expected_bench is None:
            LOGGER.error('Could not retrieve bench from git history. Exiting.')
            return
        else:
            LOGGER.info('Expected bench is {}.'.format(expected_bench))

        archs = ARCHS if ARCHS else get_possible_targets(CLONE_DIR)

        if not archs:
            LOGGER.info('No archs specified. Exiting.')
            return

        num_tries = dict()
        successfully_completed = dict()
        for arch in archs:
            num_tries[arch] = 0
            successfully_completed[arch] = False

        # Compute how many threads each worker can use
        # Try to find as equal distribution as possible
        num_workers = min([NUM_THREADS, len(archs), MAX_NUM_WORKERS])
        num_threads_per_worker = [1] * num_workers
        for i in range(NUM_THREADS - num_workers):
            num_threads_per_worker[i % num_workers] += 1
        num_workers_by_thread_id = dict()
        lock = threading.Lock()

        def run_task(arch):
            try:
                with lock:
                    thread_id = threading.get_ident()
                    if not thread_id in num_workers_by_thread_id:
                        # On thread discovery get the next available thread count
                        # We expect this to be entered at most NUM_WORKERS times
                        assert len(num_threads_per_worker) > 0
                        num_workers_by_thread_id[thread_id] = num_threads_per_worker[0]
                        num_threads_per_worker.pop(0)
                    num_threads = num_workers_by_thread_id[thread_id]

                compile_target(
                    repo_dir=CLONE_DIR,
                    out_dir=OUT_DIR,
                    build_type=BUILD_TYPE,
                    arch=arch,
                    prefix=PREFIX,
                    expected_bench=expected_bench,
                    num_threads=num_threads,
                    compiler=COMPILER)

                return True

            except Exception as e:
                LOGGER.error('Exception: {}'.format(e))
                traceback.print_exc()
                return False

        with concurrent.futures.ThreadPoolExecutor(max_workers=num_workers) as executor:
            tasks = dict()
            for arch in archs:
                LOGGER.info('Scheduling ARCH={}. Try 1 out of {}'.format(arch, MAX_TRIES))
                num_tries[arch] += 1
                future = executor.submit(run_task, arch)
                tasks[future] = arch

            for future in concurrent.futures.as_completed(tasks):
                arch = tasks[future]
                success = future.result()
                if success:
                    # If the build was successful then note it. We're done with this target
                    LOGGER.info('Sucessfully finished building {}'.format(arch))
                    successfully_completed[arch] = True
                else:
                    # If the build was not succesfull we retry, unless max number of tries was reached.
                    LOGGER.info('Failed building {} in an attempt {} out of {}'.format(arch, num_tries[arch], MAX_TRIES))
                    if num_tries[arch] < MAX_TRIES:
                        LOGGER.info('Scheduling ARCH={}. Try {} out of {}'.format(arch, num_tries[arch]+1, MAX_TRIES))
                        num_tries[arch] += 1
                        future = executor.submit(run_task, arch)
                        tasks[future] = arch

    num_successful = list(successfully_completed.values()).count(True)
    num_failed = len(successfully_completed) - num_successful
    msg = 'Finished all tasks. {} successful, {} failed.'.format(num_successful, num_failed)
    if num_failed == 0:
        LOGGER.info(msg)
    else:
        LOGGER.info(msg)

    for arch, success in sorted(successfully_completed.items(), key=lambda x: -int(x[1])):
        msg = '{}: {} after {} tries.'.format(arch, 'Success' if success else 'Failed', num_tries[arch])
        if success:
            LOGGER.info(msg)
        else:
            LOGGER.error(msg)

    if num_failed == 0:
        return 0
    else:
        return 1

if __name__ == '__main__':
    sys.exit(main())
