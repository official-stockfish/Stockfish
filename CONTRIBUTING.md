# Contributing to this Stockfish Fork

Thank you for your interest!

This repository contains both the official Stockfish engine and three original GUIs.

## Engine changes

- Functional changes that affect search strength **must** be validated on [Fishtest](https://tests.stockfishchess.org) before they can be considered for upstream.
- Non-functional changes (style, docs, refactoring) can be submitted as ordinary PRs.
- Follow the coding style enforced by `.clang-format` (`make format`).
- See the official [Developers](https://github.com/official-stockfish/Stockfish/wiki/Developers) page and the original CONTRIBUTING notes.

## GUI / front-end changes

Contributions to `matrix-gui/`, `Avalonia UI/`, and `neuralchess-pwa/` are very welcome:

- Bug fixes, new features, better theming, accessibility, packaging, tests, etc.
- Keep the code clean and consistent with the existing style of each sub-project.
- Update the corresponding README when you add significant functionality.

## Documentation

Improvements to the root README, the `docs/` folder, or the wiki are highly appreciated.

## Pull Requests

1. Fork the repository.
2. Create a feature branch.
3. Make your changes.
4. Open a PR against `master` with a clear description.

First-time contributors should add themselves to `AUTHORS` if they touch the engine.

## License

By contributing you agree that your work is licensed under GPL-3.0 (same as Stockfish).

Thank you!
