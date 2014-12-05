#!/usr/bin/env python3
"""Script that generates the build.ninja for Stockfish"""
import argparse, sys, os
import ninja_syntax

# Files to compile
objs = ['benchmark.o', 'bitbase.o', 'bitboard.o', 'endgame.o',
    'evaluate.o', 'main.o', 'material.o', 'misc.o', 'movegen.o',
    'movepick.o', 'pawns.o', 'position.o', 'search.o', 'thread.o',
    'timeman.o','tt.o','uci.o','ucioption.o','syzygy/tbprobe.o']
exe = 'stockfish'

# Auto-detect features
bits = 64 if sys.maxsize > 2**32 else 32
features = os.popen("grep flags /proc/cpuinfo | head -1 | cut -d\: -f2").read().split()

# Command line options
compilers = {'gcc': 'g++', 'clang': 'clang++'}
parser = argparse.ArgumentParser(description='Generates a Ninja build script for Stockfish.')
parser.add_argument('--compiler', type=str, choices=compilers.keys(), default='gcc',
    help='compiler: default = gcc')
parser.add_argument('--bits', type=int, choices=[32, 64], default=bits,
    help='bits: default = ' + str(bits))
parser.add_argument('--popcnt', type=str, choices=['True', 'False'],
    default=str('popcnt' in features), help='popcnt: default = ' + str('popcnt' in features))
parser.add_argument('--debug', action='store_true', help='debug compile')
parser.add_argument('--no-optimize', action='store_false', dest='optimize',
    help='disable compiler optimizations')
parser.add_argument('--file', type=str, default='build.ninja',
    help='output file (default = build.ninja)')
args = parser.parse_args()

# Open .ninja file
n = ninja_syntax.Writer(open(args.file, 'w'))
n.variable('ninja_required_version', '1.2')

# Compiler and flags
cxx = compilers[args.compiler]
n.variable('cxx', cxx)
warnings = '-Wall -Wextra -pedantic -Wcast-qual -std=c++03 -Wno-long-long -Wshadow '
cflags = warnings + '-fno-exceptions -fno-rtti -DUSE_BSFQ '
if cxx == 'g++':
    cflags += '-flto '
if not args.debug:
    cflags += '-DNDEBUG '
if args.optimize:
    cflags += '-O3 '
if args.bits == 64:
    cflags += '-DIS_64BIT '
if args.popcnt == 'True':
    cflags += '-msse3 -mpopcnt -DUSE_POPCNT '
else:
    cflags += '-msse2 '
n.variable('cflags', cflags)
n.variable('lflags', '-lpthread $cflags')
n.newline()

# Rules
n.rule('compile', description = 'Compiling $in',
    command = '$cxx -MMD -MT $out -MF $out.d $cflags -c $in -o $out',
    depfile = '$out.d', deps = 'gcc')
n.rule('link', description = 'Linking $out',
    command = '$cxx -o $out $in $lflags && strip $out')
n.newline()

# Targets
for obj in objs:
    n.build(['src/' + obj], 'compile', ['src/' + obj.replace('.o', '.cpp')])
n.build([exe], 'link', objs)
n.newline()
n.default([exe])
