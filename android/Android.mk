include $(CLEAR_VARS)

# override strip command to strip all symbols from output library; no need to ship with those..
# cmd-strip = $(TOOLCHAIN_PREFIX)strip $1 

LOCAL_ARM_MODE  := arm
LOCAL_PATH      := $(NDK_PROJECT_PATH)
LOCAL_MODULE    := stockfish
#LOCAL_CFLAGS    := -Werror
LOCAL_SRC_FILES := \
    ../src/benchmark.cpp ../src/bitbase.cpp ../src/bitboard.cpp \
    ../src/endgame.cpp ../src/evaluate.cpp ../src/main.cpp \
    ../src/material.cpp ../src/misc.cpp ../src/movegen.cpp \
    ../src/movepick.cpp ../src/pawns.cpp ../src/position.cpp \
    ../src/psqt.cpp ../src/search.cpp ../src/thread.cpp \
    ../src/timeman.cpp ../src/tt.cpp ../src/uci.cpp \
    ../src/ucioption.cpp ../src/syzygy/tbprobe.cpp
#LOCAL_LDLIBS    := -llog
LOCAL_STATIC_LIBRARIES := stlport
LOCAL_SHARED_LIBRARIES := pthreads

include $(BUILD_SHARED_LIBRARY)
