cmake_minimum_required(VERSION 3.0)
project(jstockfish)

#-------------------------------------------------------------------------------
# Build jstockfish.jar

# http://www.cmake.org/cmake/help/v3.0/module/FindJava.html
find_package(Java REQUIRED)
if (Java_FOUND)
    message (STATUS "Java_JAVAH_EXECUTABLE=${Java_JAVAH_EXECUTABLE}")
endif()

include(UseJava)
set(CMAKE_JAVA_COMPILE_FLAGS "-source" "1.6" "-target" "1.6")
file(GLOB JAVA_SOURCES "src/main/java/jstockfish/*.java")
add_jar(javastockfish ${JAVA_SOURCES} OUTPUT_NAME jstockfish)

#-------------------------------------------------------------------------------
# Build libjstockfish.dylib/so/dll

# http://www.cmake.org/cmake/help/v3.0/module/FindJNI.html
find_package(JNI REQUIRED)
if (JNI_FOUND)
    message (STATUS "JNI_INCLUDE_DIRS=${JNI_INCLUDE_DIRS}")
    message (STATUS "JNI_LIBRARIES=${JNI_LIBRARIES}")
endif()

# Generate JNI .h files
execute_process(
    COMMAND ${Java_JAVAH_EXECUTABLE} -verbose -jni
        -cp ${CMAKE_CURRENT_LIST_DIR}/src/main/java
        -d  ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp
        jstockfish.Uci

    COMMAND ${Java_JAVAH_EXECUTABLE} -verbose -jni
        -cp ${CMAKE_CURRENT_LIST_DIR}/src/main/java
        -d  ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp
        jstockfish.Position
)

include_directories(${JNI_INCLUDE_DIRS} "../src" "src/main/cpp")

# Exclude misc_sync_cout.cpp because we'll implement uci_out to
# redirect Stockfish output to Java side
file(GLOB JNI_SOURCES "../src/*.cpp" "../src/syzygy/tbprobe.cpp" "src/main/cpp/*.cpp")
list(REMOVE_ITEM JNI_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../src/misc_sync_cout.cpp")

set(CMAKE_CXX_STANDARD 11)

# See Stockfish's Makefile "make build ARCH=x86-64-modern"
set(STOCKFISH_FLAGS "-Wall -Wcast-qual -fno-exceptions -fno-rtti -std=c++11 -pedantic -Wextra -Wshadow -m64 -DNDEBUG -O3 -DIS_64BIT -msse -msse3 -mpopcnt -DUSE_POPCNT -flto")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${STOCKFISH_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${STOCKFISH_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${STOCKFISH_FLAGS}")

add_library(jstockfish SHARED ${JNI_SOURCES} ${JNI_LIBRARIES})
