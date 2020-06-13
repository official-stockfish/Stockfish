### Overview
[![Build Status](https://travis-ci.org/glinscott/fishtest.svg?branch=master)](https://travis-ci.org/glinscott/fishtest)

Fishtest is a distributed task queue for testing chess engines. The main instance
for testing the chess engine [Stockfish](https://github.com/official-stockfish/Stockfish) is at this web page https://tests.stockfishchess.org

Developers submit patches with new ideas and improvements, CPU contributors install a fishtest worker on their computers to play some chess games in the background to help the developers testing the patches.

The fishtest worker:
- automatically connects to the server to download a chess opening book, the [cutechess-cli](https://github.com/cutechess/cutechess) chess game manager and the chess engine sources (both for the current Stockfish and for the patch with the new idea). The sources will be compiled according to the type of the worker platform.
- starts a batch of games using cutechess-cli.
- uploads the games results on the server.

The fishtest server:
- manages the queue of the tests with customizable priorities.
- computes several probabilistic values from the game results sent by the workers.
- updates and publishes the results of ongoing tests.
- knows how to stop tests when they are statistically significant and publishes the final tests results.

To get more information, such as the worker/server install and configuration instructions, visit the [Fishtest Wiki](https://github.com/glinscott/fishtest/wiki).
