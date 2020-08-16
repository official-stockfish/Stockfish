# Stockfish, a UCI chess playing engine derived from Glaurung 2.1
#
# Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
# Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
# Copyright (C) 2015-2019 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad
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

include(CheckCXXCompilerFlag)
include(CheckIncludeFile)
include(CheckIPOSupported)

function(ENABLE_OPTIMIZATION target flag var)
    check_cxx_compiler_flag(${flag} ${var})
    if (${${var}})
        target_compile_options(${target} PRIVATE ${flag})
        target_link_options(${target} PRIVATE ${flag})
        target_compile_definitions(${target} PRIVATE ${var})
    endif()
endfunction()

function(ENABLE_FLAGS target)
    target_include_directories(${target}
        PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    )
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:
            -pedantic 
            -Wall 
            -Wcast-qual 
            -Wextra
            -Wshadow
            -fno-exceptions
        >
        $<$<CXX_COMPILER_ID:Intel>:
            -diag-disable 1476,10120 
            -Wcheck 
            -Wabi 
            -Wdeprecated 
            -strict-ansi
        >
    )

    if(MINGW)
        target_link_options(${target} PRIVATE -static)
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_definitions(${target} PRIVATE IS_64BIT)
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-m64>
            $<$<CXX_COMPILER_ID:AppleClang>:-arch x86_64>
        )
        target_link_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-m64>
            $<$<CXX_COMPILER_ID:AppleClang>:-arch x86_64>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-m32>
            $<$<CXX_COMPILER_ID:AppleClang>:-arch i386>
        )
        target_link_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-m32>
            $<$<CXX_COMPILER_ID:AppleClang>:-arch i386>
        )
    endif()

    enable_optimization(${target} -mneon USE_NEON)
    enable_optimization(${target} -msse4.2 USE_SSE42)
    enable_optimization(${target} -msse4.1 USE_SSE41)
    enable_optimization(${target} -mssse3 USE_SSSE3)
    enable_optimization(${target} -msse2 USE_SSE2)
    enable_optimization(${target} -mbmi2 USE_PEXT)
    enable_optimization(${target} -mavx512vnni USE_VNNI)
    enable_optimization(${target} -mavx512bw USE_AVX512)
    enable_optimization(${target} -mavx2 USE_AVX2)

    if(USE_POPCNT)
        target_compile_definitions(${target} PRIVATE USE_POPCNT)
        enable_optimization(${target} -mpopcnt USE_POPCOUNT)
        enable_optimization(${target} -msse3 USE_SSE3)
    endif()

    check_include_file("xmmintrin.h" USE_PREFETCH)
    if(USE_PREFETCH)
        enable_optimization(${target} -msse USE_SSE)
        target_compile_definitions(${target} PRIVATE USE_PREFETCH)
    else()
        target_compile_definitions(${target} PRIVATE NO_PREFETCH)
    endif()

    if(ANDROID)
        target_compile_options(${target} PRIVATE -fPIE)
        target_link_options(${target} PRIVATE -fPIE -pie)
    elseif(APPLE)
        target_compile_options(${target} PRIVATE -mmacosx-version-min=10.15)
        target_link_options(${target} PRIVATE -mmacosx-version-min=10.15)
    endif()

    if (NOT APPLE)
        target_link_options(${target} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-Wl,--no-as-needed>)
    endif()

    find_library(ATOMIC_LIBRARY atomic)
    if (ATOMIC_LIBRARY)
        target_link_libraries(${target} INTERFACE ${ATOMIC_LIBRARY})
    endif()

    # Use Win32 threads on MinGW.
    # Android and Haiku have pthreads bundled in.
    find_package(Threads REQUIRED)
    if (NOT MINGW AND NOT ANDROID AND NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku")
        target_link_libraries(${target} INTERFACE Threads::Threads)
    endif()
endfunction()

function(ENABLE_OPTIMIZE target)
    target_compile_definitions(${target} PRIVATE NDEBUG)

    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-O3>
    )

    if(ANDROID)
        target_compile_options(${target} PRIVATE
            -fno-gcse
            -mthumb
            -march=armv7-a
            -mfloat-abi=softfp
        )
    elseif(APPLE)
        target_compile_options(${target} PRIVATE -mdynamic-no-pic)
    endif()
endfunction()

function(ENABLE_LTO target)
    check_ipo_supported(RESULT result OUTPUT output)
    if(result)
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO is not supported: ${output}")
    endif()
endfunction()

function(ENABLE_SANITIZER target sanitizer)
    if(NOT sanitizer MATCHES "^(address|thread|undefined)$")
        message(ERROR "Unrecognized sanitizer: ${sanitizer}")
    else()
        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-fsanitize=${sanitizer}>
        )
        target_link_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-fsanitize=${sanitizer}>
        )
    endif()
endfunction()

function(ENABLE_PROFILING target type)
    string(TOUPPER "${type}" pgo_type)

    if(pgo_type STREQUAL "GENERATE")
        set(ENABLE_GENERATE TRUE)
    elseif(pgo_type STREQUAL "USE")
        set(ENABLE_USE TRUE)
    else()
        message(SEND_ERROR "Unrecognized option for ENABLE_PROFILE: ${type}")
        return()
    endif()

    get_target_property(source_dir ${target} SOURCE_DIR)
    set(PGO_DIR "${source_dir}/pgo_data/")

    set(CLANG_GENERATE "${PGO_DIR}/stockfish.profraw")
    set(CLANG_USE "${PGO_DIR}/stockfish.profdata")
    set(GCC_GENERATE "${PGO_DIR}")
    set(GCC_USE "${PGO_DIR}")
    set(INTEL_GENERATE "${PGO_DIR}")
    set(INTEL_USE "${PGO_DIR}")

    execute_process(
        COMMAND gcc -print-file-name=libgcov.a
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
        OUTPUT_VARIABLE GCOV_LIBRARY
    )

    # Generate profiling data.
    if(ENABLE_GENERATE)
        file(MAKE_DIRECTORY ${PGO_DIR})

        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang>:-fprofile-instr-generate=${CLANG_GENERATE}>
            $<$<CXX_COMPILER_ID:GNU>:-fprofile-dir=${GCC_GENERATE} -fprofile-generate>
            $<$<CXX_COMPILER_ID:Intel>:-prof-gen=srcpos -prof_dir ${INTEL_GENERATE}>
        )
        target_link_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang>:-fprofile-instr-generate=${CLANG_GENERATE}>
        )

        target_link_libraries(${target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU>:${GCOV_LIBRARY}>
        )

        get_target_property(target_type ${target} TYPE)

        if(target_type STREQUAL "EXECUTABLE")
            if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND $<TARGET_FILE:${target}> bench
                    COMMENT "Running profiling test..."
                    VERBATIM
                )
            elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND LLVM_PROFILE_FILE=${CLANG_GET} $<TARGET_FILE:${target}> bench
                    COMMENT "Running profiling test..."
                    VERBATIM
                )

                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND llvm-profdata merge -output=${CLANG_USE} ${CLANG_GENERATE}
                    COMMENT "Creating profile data..."
                    VERBATIM
                )
            endif()
        endif()
    endif()

    # Use profiling data.
    if(ENABLE_USE)
        if(NOT EXISTS ${PGO_DIR})
            message(SEND_ERROR "No profiling data found")
            return()
        endif()

        target_compile_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang>:-fprofile-instr-use=${CLANG_USE}>
            $<$<CXX_COMPILER_ID:GNU>:-fprofile-dir=${GCC_USE} -fprofile-use -fno-peel-loops -fno-tracer>
            $<$<CXX_COMPILER_ID:Intel>:-prof_use -prof_dir ${INTEL_USE}>
        )
        target_link_options(${target} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang>:-fprofile-instr-use=${CLANG_USE}>
        )
        target_link_libraries(${target} PRIVATE
            $<$<CXX_COMPILER_ID:GNU>:${GCOV_LIBRARY}>
        )
    endif()
endfunction()
