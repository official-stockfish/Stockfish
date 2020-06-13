#!/usr/bin/python

from setuptools import setup, find_packages

requires = [
    'requests'
    ]

setup(
    name="fishtest_worker",
    version="0.1",
    packages=find_packages(),
    install_requires=requires,
    test_suite="test_worker"
)
