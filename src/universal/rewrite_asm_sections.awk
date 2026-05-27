# Stockfish, a UCI chess playing engine derived from Glaurung 2.1
# Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)
#
# Stockfish is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Stockfish is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

# This is used if the universal binary is compiled on clang+Windows.
# Usage (avx2 example):
#   awk -v MODNAME=x86_64_avx2 -f rewrite_asm_sections.awk stockfish.exe.lto.s > renamed.s
#
# The relevant output from --lto-emit-asm looks like:
#  ...
#    .section    .ctors,"dw",associative,_ZN21Stockfish_x86_64_avx217SYSTEM_THREADS_NBE,unique,0
#    .p2align    3, 0x0
#    .quad    __cxx_global_var_init
#    .section    .ctors,"a",unique,0
#    .p2align    3, 0x0
#    .quad    _GLOBAL__sub_I_benchmark.cpp
#    .quad    _GLOBAL__sub_I_evaluate.cpp
#  ...
#    .quad    _GLOBAL__sub_I_engine.cpp
#    .quad    _GLOBAL__sub_I_score.cpp
# 
#    .section    .bss$_ZN21Stockfish_x86_64_avx2L26STARTUP_PROCESSOR_AFFINITYE,"bw",one_only,_ZN21Stockfish_x86_64_avx2L26STARTUP_PROCESSOR_AFFINITYE,unique,570
#  ...
# 
# We want to consolidate everything into a single section named x86_64_avx2_init, and insert
# symbols __start_x86_64_avx2_init/__stop_x86_64_avx2_init for entry_x86.cpp to use.
# (On ELF these symbols are implicitly defined, so naming it this way keeps the universal
# entry point consistent.)

BEGIN {
    in_ctors = 0
}

# Match any .section line containing .ctors
/^[[:space:]]*\.section[[:space:]].*\.ctors/ {

    # First .ctors section, emit start symbol
    if (!in_ctors) {
        in_ctors = 1
        section = MODNAME "_init"

        printf "\t.section\t%s,\"a\"\n", section
        printf "\t.globl\t__start_%s\n", section
        printf "__start_%s:\n", section
    }

    # Suppress all original .ctors section directives
    next
}

# First non-.ctors .section after ctors block, emit stop symbol
in_ctors && /^[[:space:]]*\.section/ {
    section = MODNAME "_init"

    printf "\t.globl\t__stop_%s\n", section
    printf "__stop_%s:\n", section

    in_ctors = 0
}

{
    print
}

END {
    if (in_ctors) {
        section = MODNAME "_init"
        printf "\t.globl\t__stop_%s\n", section
        printf "__stop_%s:\n", section
    }
}
