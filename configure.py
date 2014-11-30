#!/usr/bin/python
"""Script that generates the build.ninja for Stockfish"""
from optparse import OptionParser
import sys
import ninja_syntax

# Files to compile
object_files = ['src/benchmark.o', 'src/bitbase.o', 'src/bitboard.o', 'src/endgame.o',
    'src/evaluate.o', 'src/main.o', 'src/material.o', 'src/misc.o', 'src/movegen.o',
    'src/movepick.o', 'src/pawns.o', 'src/position.o', 'src/search.o', 'src/thread.o',
    'src/timeman.o','src/tt.o','src/uci.o','src/ucioption.o','src/syzygy/tbprobe.o']

# Command line options
compilers = {'gcc': 'g++','clang': 'clang++'}
parser = OptionParser()
parser.add_option('-c', '--compiler', choices = compilers.keys(), default = 'gcc',
    help = 'compiler (' + '/'.join(compilers.keys()) + ')')
parser.add_option('-f', '--file', help = 'output ninja file (default=build.ninja)')
parser.add_option('-d', '--debug', action = 'store_true', help = 'build a debug compile')
(options, args) = parser.parse_args()
if args:
    print('ERROR: unknown argument(s):', args)
    sys.exit(1)

# Open .ninja file
n = ninja_syntax.Writer(open(options.file, 'w'))
n.variable('ninja_required_version', '1.2')
n.newline()

# Compiler and flags
cxx = compilers[options.compiler]
n.variable('cxx', cxx)
cflags = ['-DSYZYGY', '-Wall', '-Wcast-qual', '-fno-exceptions', '-fno-rtti', '-pedantic',
    '-Wno-long-long', '-Wextra', '-Wshadow', '-O3', '-DIS_64BIT', '-msse', '-DUSE_BSFQ',
    '-msse3', '-mpopcnt', '-DUSE_POPCNT']
if cxx == 'g++':
    cflags.append('-flto')
if not options.debug:
    cflags.append('-DNDEBUG')
n.variable('cflags', ' '.join(cflags))
n.variable('lflags', '-lpthread $cflags')
n.newline()

# Rules
n.rule('compile', description = 'Compiling $in',
    command = '$cxx -MMD -MT $out -MF $out.d $cflags -c $in -o $out',
    depfile = '$out.d', deps = 'gcc')
n.newline()
n.rule('link', description = 'Linking $out',
    command = '$cxx -o $out $in $lflags && strip $out')
n.newline()

# Targets
for obj in object_files:
    n.build(obj, 'compile', obj.replace('.o', '.cpp'))
n.newline()
n.build('stockfish', 'link', object_files)
n.newline()
n.default('stockfish')

n.close()
