/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "../src/shm.h"

#include <string>
#include <vector>

#include <windows.h>

namespace {

class ChildProcess {
   public:
    ~ChildProcess() {
        if (info.hProcess)
        {
            TerminateProcess(info.hProcess, 0);
            WaitForSingleObject(info.hProcess, 5000);
            CloseHandle(info.hProcess);
        }
        if (info.hThread)
            CloseHandle(info.hThread);
    }

    PROCESS_INFORMATION info{};
};

int run_child(const char* mutexName, const char* eventName) {
    HANDLE mutex = CreateMutexA(nullptr, TRUE, mutexName);
    HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, eventName);
    if (!mutex || !event)
        return 20;

    if (!SetEvent(event))
        return 21;

    Sleep(INFINITE);
    return 22;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--child")
        return run_child(argv[2], argv[3]);
    if (argc != 1)
        return 1;

    const std::string mappingName =
      "Local\\stockfish_shm_abandoned_test_" + std::to_string(GetCurrentProcessId());
    const std::string mutexName = mappingName + "$mutex";
    const std::string eventName = mappingName + "$ready";

    HANDLE ready = CreateEventA(nullptr, TRUE, FALSE, eventName.c_str());
    if (!ready)
        return 2;

    char  executable[MAX_PATH];
    DWORD executableLength = GetModuleFileNameA(nullptr, executable, sizeof(executable));
    if (executableLength == 0 || executableLength == sizeof(executable))
    {
        CloseHandle(ready);
        return 3;
    }

    std::string command =
      "\"" + std::string(executable) + "\" --child \"" + mutexName + "\" \"" + eventName + "\"";
    std::vector<char> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    ChildProcess child;
    if (!CreateProcessA(nullptr, commandBuffer.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                        &startup, &child.info))
    {
        CloseHandle(ready);
        return 4;
    }

    if (WaitForSingleObject(ready, 5000) != WAIT_OBJECT_0)
    {
        CloseHandle(ready);
        return 5;
    }
    CloseHandle(ready);

    // Keep the mutex object alive after terminating its owning process so the
    // next waiter deterministically receives WAIT_ABANDONED.
    HANDLE keepAlive = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, mutexName.c_str());
    if (!keepAlive)
        return 6;

    if (!TerminateProcess(child.info.hProcess, 0)
        || WaitForSingleObject(child.info.hProcess, 5000) != WAIT_OBJECT_0)
    {
        CloseHandle(keepAlive);
        return 7;
    }

    Stockfish::SharedMemoryBackend<int> shared(mappingName, 1234);
    CloseHandle(keepAlive);

    if (!shared.is_valid())
        return 8;
    return *static_cast<int*>(shared.get()) == 1234 ? 0 : 9;
}
