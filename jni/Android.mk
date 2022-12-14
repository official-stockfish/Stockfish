LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := Stockfish
LOCAL_C_INCLUDES := 
LOCAL_SRC_FILES := $(wildcard ../src/*.cpp ../src/nnue/*.cpp ../src/nnue/features/*.cpp ../src/syzygy/*.cpp)
LOCAL_CFLAGS += -std=c++17 -O3 -flto -fexperimental-new-pass-manager -Wfatal-errors -Wall -Wextra -Wshadow -DNNUE_EMBEDDING_OFF -DUSE_PTHREADS
LOCAL_CPPFLAGS += 
LOCAL_CPP_FEATURES := 

ifeq ($(TARGET_ARCH_ABI), arm64-v8a)
  LOCAL_CFLAGS += -DIS_64BIT -DUSE_NEON=8 -DUSE_POPCNT
endif
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
  LOCAL_CFLAGS += -DUSE_NEON=7 -DUSE_POPCNT
endif

LOCAL_LDFLAGS +=
include $(BUILD_EXECUTABLE)