import subprocess
from typing import List, Dict
import os
import collections
import time
import sys
import traceback
import fnmatch
from functools import wraps
from contextlib import redirect_stdout
import io
import concurrent.futures
import tempfile
import statistics

CYAN_COLOR = "\033[36m"
GRAY_COLOR = "\033[2m"
RED_COLOR = "\033[31m"
GREEN_COLOR = "\033[32m"
YELLOW_COLOR = "\033[33m"
RESET_COLOR = "\033[0m"
WHITE_BOLD = "\033[1m"

MAX_TIMEOUT = 60 * 5

class OrderedClassMembers(type):
    @classmethod
    def __prepare__(self, name, bases):
        return collections.OrderedDict()

    def __new__(self, name, bases, classdict):
        classdict["__ordered__"] = [
            key for key in classdict.keys() if key not in ("__module__", "__qualname__")
        ]
        return type.__new__(self, name, bases, classdict)

class TimeoutException(Exception):
    def __init__(self, message: str, timeout: int):
        self.message = message
        self.timeout = timeout

def timeout_decorator(timeout: float):
    def decorator(func):
        @wraps(func)
        def wrapper(*args, **kwargs):
            with concurrent.futures.ThreadPoolExecutor() as executor:
                future = executor.submit(func, *args, **kwargs)
                try:
                    result = future.result(timeout=timeout)
                except concurrent.futures.TimeoutError:
                    raise TimeoutException(
                        f"Function {func.__name__} timed out after {timeout} seconds",
                        timeout,
                    )
            return result
        return wrapper
    return decorator

class TestResult:
    def __init__(self, name: str, status: str, duration: float, error_message: str = None):
        self.name = name
        self.status = status
        self.duration = duration
        self.error_message = error_message

class TestSuiteResult:
    def __init__(self, name: str):
        self.name = name
        self.results: List[TestResult] = []
        self.start_time = time.time()
        self.end_time = None

    def add_result(self, result: TestResult):
        self.results.append(result)

    def finish(self):
        self.end_time = time.time()

    @property
    def duration(self):
        return self.end_time - self.start_time if self.end_time else 0

    @property
    def passed_tests(self):
        return sum(1 for result in self.results if result.status == "PASS")

    @property
    def failed_tests(self):
        return sum(1 for result in self.results if result.status == "FAIL")

class MiniTestFramework:
    def __init__(self):
        self.test_suite_results: List[TestSuiteResult] = []

    def run(self, classes: List[type]) -> bool:
        self.start_time = time.time()

        for test_class in classes:
            with tempfile.TemporaryDirectory() as tmpdirname:
                original_cwd = os.getcwd()
                os.chdir(tmpdirname)

                try:
                    self.__run(test_class)
                finally:
                    os.chdir(original_cwd)

        self.end_time = time.time()
        self.__print_summary()
        return self.has_failed()

    def has_failed(self) -> bool:
        return any(suite.failed_tests > 0 for suite in self.test_suite_results)

    def __run(self, test_class) -> None:
        test_instance = test_class()
        test_name = test_instance.__class__.__name__
        test_methods = [m for m in test_instance.__ordered__ if m.startswith("test_")]

        print(f"\nTest Suite: {test_name}")

        suite_result = TestSuiteResult(test_name)

        if hasattr(test_instance, "beforeAll"):
            test_instance.beforeAll()

        for method in test_methods:
            result = self.__run_test_method(test_instance, method)
            suite_result.add_result(result)

        if hasattr(test_instance, "afterAll"):
            test_instance.afterAll()

        suite_result.finish()
        self.test_suite_results.append(suite_result)

    def __run_test_method(self, test_instance, method: str) -> TestResult:
        print(f"    Running {method}... ", end="", flush=True)

        buffer = io.StringIO()
        start_time = time.time()

        try:
            with redirect_stdout(buffer):
                if hasattr(test_instance, "beforeEach"):
                    test_instance.beforeEach()

                getattr(test_instance, method)()

                if hasattr(test_instance, "afterEach"):
                    test_instance.afterEach()

            duration = time.time() - start_time
            self.print_success(f"{method} ({duration:.2f}s)")
            return TestResult(method, "PASS", duration)
        except Exception as e:
            duration = time.time() - start_time
            if isinstance(e, TimeoutException):
                self.print_failure(f"{method} (hit execution limit of {e.timeout} seconds)")
                error_message = f"Timeout: {e.message}"
            elif isinstance(e, AssertionError):
                self.print_failure(f"{method} ({duration:.2f}s)")
                error_message = str(e)
            else:
                self.print_failure(f"{method} ({duration:.2f}s)")
                error_message = traceback.format_exc()

            self.__print_error(error_message)
            return TestResult(method, "FAIL", duration, error_message)
        finally:
            self.__print_buffer_output(buffer)

    def __print_error(self, error_message: str):
        colored_traceback = "\n".join(
            f"  {CYAN_COLOR}{line}{RESET_COLOR}"
            for line in error_message.splitlines()
        )
        print(colored_traceback)

    def __print_buffer_output(self, buffer: io.StringIO):
        output = buffer.getvalue()
        if output:
            indented_output = "\n".join(f"    {line}" for line in output.splitlines())
            print(f"    {YELLOW_COLOR}⎯⎯⎯⎯⎯OUTPUT⎯⎯⎯⎯⎯{RESET_COLOR}")
            print(f"{GRAY_COLOR}{indented_output}{RESET_COLOR}")
            print(f"    {YELLOW_COLOR}⎯⎯⎯⎯⎯OUTPUT⎯⎯⎯⎯⎯{RESET_COLOR}")

    def __print_summary(self):
        total_duration = self.end_time - self.start_time
        total_tests = sum(len(suite.results) for suite in self.test_suite_results)
        total_passed = sum(suite.passed_tests for suite in self.test_suite_results)
        total_failed = sum(suite.failed_tests for suite in self.test_suite_results)

        print(f"\n{WHITE_BOLD}Test Summary{RESET_COLOR}\n")
        print(f"    Total Duration: {total_duration:.2f}s")
        print(f"    Test Suites: {len(self.test_suite_results)} total")
        print(f"    Tests: {GREEN_COLOR}{total_passed} passed{RESET_COLOR}, {RED_COLOR}{total_failed} failed{RESET_COLOR}, {total_tests} total")

        if total_failed > 0:
            print(f"\n{WHITE_BOLD}Failed Tests:{RESET_COLOR}")
            for suite in self.test_suite_results:
                for result in suite.results:
                    if result.status == "FAIL":
                        print(f"    {RED_COLOR}{suite.name}.{result.name}{RESET_COLOR} ({result.duration:.2f}s)")

        print(f"\n{WHITE_BOLD}Test Suite Details:{RESET_COLOR}")
        for suite in self.test_suite_results:
            print(f"\n    {suite.name}:")
            print(f"        Duration: {suite.duration:.2f}s")
            print(f"        Tests: {GREEN_COLOR}{suite.passed_tests} passed{RESET_COLOR}, {RED_COLOR}{suite.failed_tests} failed{RESET_COLOR}, {len(suite.results)} total")
            
            test_durations = [result.duration for result in suite.results]
            if test_durations:
                print(f"        Fastest test: {min(test_durations):.2f}s")
                print(f"        Slowest test: {max(test_durations):.2f}s")
                print(f"        Average test duration: {statistics.mean(test_durations):.2f}s")

    def print_failure(self, message: str):
        print(f"{RED_COLOR}✗ {message}{RESET_COLOR}", flush=True)

    def print_success(self, message: str):
        print(f"{GREEN_COLOR}✓ {message}{RESET_COLOR}", flush=True)


class Stockfish:
    def __init__(
        self,
        prefix: List[str],
        path: str,
        args: List[str] = [],
        cli: bool = False,
    ):
        self.path = path
        self.process = None
        self.args = args
        self.cli = cli
        self.prefix = prefix
        self.output = []

        self.start()

    def start(self):
        if self.cli:
            self.process = subprocess.run(
                self.prefix + [self.path] + self.args,
                capture_output=True,
                text=True,
            )

            self.process.stdout

            return

        self.process = subprocess.Popen(
            self.prefix + [self.path] + self.args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
            bufsize=1,
        )

    def setoption(self, name: str, value: str):
        self.send_command(f"setoption name {name} value {value}")

    def send_command(self, command: str):
        if not self.process:
            raise RuntimeError("Stockfish process is not started")

        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    @timeout_decorator(MAX_TIMEOUT)
    def equals(self, expected_output: str):
        for line in self.readline():
            if line == expected_output:
                return

    @timeout_decorator(MAX_TIMEOUT)
    def expect(self, expected_output: str):
        for line in self.readline():
            if fnmatch.fnmatch(line, expected_output):
                return

    @timeout_decorator(MAX_TIMEOUT)
    def contains(self, expected_output: str):
        for line in self.readline():
            if expected_output in line:
                return

    @timeout_decorator(MAX_TIMEOUT)
    def starts_with(self, expected_output: str):
        for line in self.readline():
            if line.startswith(expected_output):
                return

    @timeout_decorator(MAX_TIMEOUT)
    def check_output(self, callback):
        if not callback:
            raise ValueError("Callback function is required")

        for line in self.readline():
            if callback(line) == True:
                return

    def readline(self):
        if not self.process:
            raise RuntimeError("Stockfish process is not started")

        while True:
            line = self.process.stdout.readline().strip()
            self.output.append(line)

            yield line

    def clear_output(self):
        self.output = []

    def get_output(self) -> List[str]:
        return self.output

    def quit(self):
        self.send_command("quit")

    def close(self):
        if self.process:
            self.process.stdin.close()
            self.process.stdout.close()
            return self.process.wait()

        return 0
