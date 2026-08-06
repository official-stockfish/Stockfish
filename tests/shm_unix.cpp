/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2026 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/

#include "../src/shm_unix.h"

#include <cinttypes>
#include <cstdio>
#include <optional>
#include <string>

#include <sys/wait.h>
#include <unistd.h>

namespace {

using Stockfish::shm::SharedMemory;

int run_child(const std::string& name) {
    auto shared = Stockfish::shm::create_shared<int>(name, 333);
    if (!shared)
        return 20;
    return shared->get() == 111 ? 0 : 21;
}

struct TempDirectoryCleanup {
    explicit TempDirectoryCleanup(const std::string& name) {
        const auto& root = Stockfish::shm::TempRoot::get_temp_root();
        if (!root)
            return;

        char sentinel[32];
        std::snprintf(sentinel, sizeof(sentinel), "sfshm_%016" PRIu64,
                      Stockfish::hash_string(name));
        directory = root->prefix + "/" + sentinel;
    }

    ~TempDirectoryCleanup() {
        if (!directory.empty())
        {
            unlink((directory + "/init_lock").c_str());
            rmdir(directory.c_str());
        }
    }

    std::string directory;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--child")
        return run_child(argv[2]);
    if (argc != 1)
        return 1;

    const std::string    name = "stockfish-shm-unix-test-" + std::to_string(getpid());
    TempDirectoryCleanup cleanup(name);

    std::optional<SharedMemory<int>> first = Stockfish::shm::create_shared<int>(name, 111);
    if (!first)
        return 2;

    auto second = Stockfish::shm::create_shared<int>(name, 222);
    if (!second)
        return 3;
    if (second->get() != 111)
        return 4;

    // Closing one same-process handle must not make the still-live first handle
    // undiscoverable to a newly launched process.
    second.reset();

    const pid_t child = fork();
    if (child == -1)
        return 5;
    if (child == 0)
    {
        execl(argv[0], argv[0], "--child", name.c_str(), nullptr);
        _exit(127);
    }

    int status = 0;
    if (waitpid(child, &status, 0) != child)
        return 6;
    if (!WIFEXITED(status))
        return 7;
    return WEXITSTATUS(status);
}
