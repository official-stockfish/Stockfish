#!/usr/bin/python
"""Script that generates the build.ninja for Stockfish"""
import argparse, sys
import ninja_syntax

# Files to compile
object_files = ['src/benchmark.o', 'src/bitbase.o', 'src/bitboard.o', 'src/endgame.o',
    'src/evaluate.o', 'src/main.o', 'src/material.o', 'src/misc.o', 'src/movegen.o',
    'src/movepick.o', 'src/pawns.o', 'src/position.o', 'src/search.o', 'src/thread.o',
    'src/timeman.o','src/tt.o','src/uci.o','src/ucioption.o','src/syzygy/tbprobe.o']

# Auto-detect features
bits = 64 if sys.maxsize > 2**32 else 32

# Command line options
compilers = {'gcc': 'g++', 'clang': 'clang++'}
parser = argparse.ArgumentParser(description = 'Generates a Ninja build script for Stockfish.')
parser.add_argument('--compiler', type = str, choices = compilers.keys(), default = 'gcc', help = 'compiler (default = gcc)')
parser.add_argument('--bits', type = int, choices = [32, 64], default = bits, help = 'bits: default = ' + str(bits))
parser.add_argument('--debug', action = 'store_true', help = 'debug compile')
parser.add_argument('--no-optimize', action = 'store_false', dest = 'optimize', help = 'disable compiler optimizations')
parser.add_argument('--file', type = str, default = 'build.ninja', help = 'output file (default = build.ninja)')
args = parser.parse_args()

# Open .ninja file
n = ninja_syntax.Writer(open(args.file, 'w'))
n.variable('ninja_required_version', '1.2')
n.newline()

# Compiler and flags
cxx = compilers[args.compiler]
n.variable('cxx', cxx)
cflags = ['-DSYZYGY', '-Wall', '-Wcast-qual', '-fno-exceptions', '-fno-rtti', '-std=c++03', '-pedantic',
    '-Wno-long-long', '-Wextra', '-Wshadow', '-DUSE_BSFQ', '-msse3', '-mpopcnt', '-DUSE_POPCNT']
if cxx == 'g++':
    cflags.append('-flto')
if args.bits == 64:
    cflags.append('-DIS_64BIT')
if not args.debug:
    cflags.append('-DNDEBUG')
if args.optimize:
    cflags.append('-O3')
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
