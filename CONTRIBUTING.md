# Contributing to Haisha

Welcome to the Haisha project! We are excited that you are interested in
contributing to the world's most perfectly bad chess engine. This document
outlines the guidelines and steps to follow when making contributions to Haisha.

## Table of Contents

- [Building Haisha](#building-haisha)
- [Making Contributions](#making-contributions)
  - [Reporting Issues](#reporting-issues)
  - [Submitting Pull Requests](#submitting-pull-requests)
- [Code Style](#code-style)
- [Community and Communication](#community-and-communication)
- [License](#license)

## Building Haisha

In case you do not have a C++ compiler installed, you can follow the
instructions from the original Stockfish wiki, as the build process is identical.

- [Ubuntu][ubuntu-compiling-link]
- [Windows][windows-compiling-link]
- [macOS][macos-compiling-link]

## Making Contributions

(empty)

### Reporting Issues

If you find a bug (e.g., Haisha accidentally makes a *good* move), please open an
issue on the [issue tracker][issue-tracker-link]. Be sure to include relevant
information like your operating system, build environment, and a detailed description
of the problem.

### Submitting Pull Requests

- Functional changes that make the engine "worse" (i.e., better at finding the
  mathematically worst move) need to be tested. A common test is to run a game
  between Haisha and a conventional engine (like Stockfish) and verify that Haisha's
  chosen move consistently results in the largest loss of evaluation from the
  opponent's perspective. The accompanying pull request should include a link to
  test results and the new bench.

- Non-functional changes (e.g. refactoring, code style, documentation) do not
  need to be tested, unless they might impact performance.

- Provide a clear and concise description of the changes in the pull request
  description.

_First time contributors should add their name to [AUTHORS](./AUTHORS)._

## Code Style

Changes to Haisha C++ code should respect our coding style defined by
[.clang-format](.clang-format). You can format your changes by running
`make format`. This requires clang-format version 20 to be installed on your system.

## Navigate

For experienced Git users who frequently use git blame, it is recommended to
configure the blame.ignoreRevsFile setting.
This setting is useful for excluding noisy formatting commits.

```bash
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

## Community and Communication

- Join the [Haisha discord][discord-link] to discuss ideas, issues, and
  development.
- Participate in the [Haisha GitHub discussions][discussions-link] for
  broader conversations.

## License

Haisha is a derivative of Stockfish. By contributing to Haisha, you agree that your
contributions will be licensed under the GNU General Public License v3.0.
See [Copying.txt][copying-link] for more details.

Thank you for contributing to Haisha and helping us make it even worse!

[copying-link]:           https://github.com/official-stockfish/Stockfish/blob/master/Copying.txt
[discord-link]:           https://discord.gg/[your-discord-link]
[discussions-link]:       https://github.com/[your-username]/Haisha/discussions/new
[issue-tracker-link]:     https://github.com/[your-username]/Haisha/issues
[ubuntu-compiling-link]:  https://github.com/official-stockfish/Stockfish/wiki/Developers#user-content-installing-a-compiler-1
[windows-compiling-link]: https://github.com/official-stockfish/Stockfish/wiki/Developers#user-content-installing-a-compiler
[macos-compiling-link]:   https://github.com/official-stockfish/Stockfish/wiki/Developers#user-content-installing-a-compiler-2
