#!/bin/bash
# check for errors under Valgrind or sanitizers.

error()
{
  echo "instrumented testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

# define suitable post and prefixes for testing options
case $1 in
  --valgrind)
    echo "valgrind testing started"
    prefix=''
    exeprefix='valgrind --error-exitcode=42 --errors-for-leak-kinds=all --leak-check=full'
    postfix=''
    threads="1"
  ;;
  --valgrind-thread)
    echo "valgrind-thread testing started"
    prefix=''
    exeprefix='valgrind --fair-sched=try --error-exitcode=42'
    postfix=''
    threads="2"
  ;;
  --sanitizer-undefined)
    echo "sanitizer-undefined testing started"
    prefix='!'
    exeprefix=''
    postfix='2>&1 | grep -A50 "runtime error:"'
    threads="1"
  ;;
  --sanitizer-thread)
    echo "sanitizer-thread testing started"
    prefix='!'
    exeprefix=''
    postfix='2>&1 | grep -A50 "WARNING: ThreadSanitizer:"'
    threads="2"

cat << EOF > tsan.supp
race:Stockfish::TTEntry::read
race:Stockfish::TTEntry::save

race:Stockfish::TranspositionTable::probe
race:Stockfish::TranspositionTable::hashfull

EOF

    export TSAN_OPTIONS="suppressions=./tsan.supp"

  ;;
  *)
    echo "unknown testing started"
    prefix=''
    exeprefix=''
    postfix=''
    threads="1"
  ;;
esac

cat << EOF > bench_tmp.epd
Rn6/1rbq1bk1/2p2n1p/2Bp1p2/3Pp1pP/1N2P1P1/2Q1NPB1/6K1 w - - 2 26
rnbqkb1r/ppp1pp2/5n1p/3p2p1/P2PP3/5P2/1PP3PP/RNBQKBNR w KQkq - 0 3
3qnrk1/4bp1p/1p2p1pP/p2bN3/1P1P1B2/P2BQ3/5PP1/4R1K1 w - - 9 28
r4rk1/1b2ppbp/pq4pn/2pp1PB1/1p2P3/1P1P1NN1/1PP3PP/R2Q1RK1 w - - 0 13
EOF

# simple command line testing
for args in "eval" \
            "go nodes 1000" \
            "go depth 10" \
            "go perft 4" \
            "go movetime 1000" \
            "go wtime 8000 btime 8000 winc 500 binc 500" \
            "go wtime 1000 btime 1000 winc 0 binc 0" \
            "go wtime 1000 btime 1000 winc 0 binc 0" \
            "go wtime 1000 btime 1000 winc 0 binc 0 movestogo 5" \
            "go movetime 200" \
            "go nodes 20000 searchmoves e2e4 d2d4" \
            "bench 128 $threads 8 default depth" \
            "bench 128 $threads 3 bench_tmp.epd depth" \
            "export_net verify.nnue" \
            "d" \
            "compiler" \
            "license" \
            "uci"
do

   echo "$prefix $exeprefix ./stockfish $args $postfix"
   eval "$prefix $exeprefix ./stockfish $args $postfix"

done

# verify the generated net equals the base net
network=`./stockfish uci | grep 'option name EvalFile type string default' | awk '{print $NF}'`
echo "Comparing $network to the written verify.nnue"
diff $network verify.nnue

# more general testing, following an uci protocol exchange
cat << EOF > game.exp
 set timeout 240
 # to correctly catch eof we need the following line
 # expect_before timeout { exit 2 } eof { exit 3 }
 expect_before timeout { exit 2 }

 spawn $exeprefix ./stockfish
 expect "Stockfish"

 send "uci\n"
 expect "uciok"

 # send "setoption name Debug Log File value debug.log\n"
 send "setoption name Threads value $threads\n"

 send "ucinewgame\n"
 send "position startpos\n"
 send "go nodes 1000\n"
 expect "bestmove"

 send "ucinewgame\n"
 send "position startpos moves e2e4 e7e6\n"
 send "go nodes 1000\n"
 expect "bestmove"

 send "ucinewgame\n"
 send "position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1\n"
 send "go depth 10\n"
 expect "bestmove"

 send "ucinewgame\n"
 send "position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1\n"
 send "flip\n"
 send "go depth 10\n"
 expect "bestmove"

 send "ucinewgame\n"
 send "position startpos\n"
 send "go depth 5\n"
 expect -re {info depth \d+ seldepth \d+ multipv \d+ score cp \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect "bestmove"

 send "ucinewgame\n"
 send "setoption name UCI_ShowWDL value true\n"
 send "position startpos\n"
 send "go depth 9\n"
 expect -re {info depth 1 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect -re {info depth 2 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect -re {info depth 3 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect -re {info depth 4 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect -re {info depth 5 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect -re {info depth 6 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect -re {info depth 7 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect -re {info depth 8 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect -re {info depth 9 seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv}
 expect "bestmove"

 send "setoption name Clear Hash\n"

 send "ucinewgame\n"
 send "position fen 5K2/8/2qk4/2nPp3/3r4/6B1/B7/3R4 w - e6\n"
 send "go depth 18\n"
 expect "score mate 1"
 expect "pv d5e6"
 expect "bestmove d5e6"

 send "ucinewgame\n"
 send "position fen 2brrb2/8/p7/Q7/1p1kpPp1/1P1pN1K1/3P4/8 b - -\n"
 send "go depth 18\n"
 expect "score mate -1"
 expect "bestmove"

 send "ucinewgame\n"
 send "position fen 7K/P1p1p1p1/2P1P1Pk/6pP/3p2P1/1P6/3P4/8 w - - 0 1\n"
 send "go nodes 500000\n"
 expect "bestmove"

 send "ucinewgame\n"
 send "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -\n"
 send "go depth 18 searchmoves c6d7\n"
 expect "score mate 2 * pv c6d7 * f7f5"
 expect "bestmove c6d7"

 send "ucinewgame\n"
 send "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -\n"
 send "go mate 2 searchmoves c6d7\n"
 expect "score mate 2 * pv c6d7"
 expect "bestmove c6d7"

 send "ucinewgame\n"
 send "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -\n"
 send "go nodes 500000 searchmoves c6d7\n"
 expect "score mate 2 * pv c6d7 * f7f5"
 expect "bestmove c6d7"

 send "ucinewgame\n"
 send "position fen 1NR2B2/5p2/5p2/1p1kpp2/1P2rp2/2P1pB2/2P1P1K1/8 b - - \n"
 send "go depth 27\n"
 expect "score mate -2"
 expect "pv d5e6 c8d8"
 expect "bestmove d5e6"

 send "ucinewgame\n"
 send "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - - moves c6d7 f2f1q\n"
 send "go depth 18\n"
 expect "score mate 1 * pv f7f5"
 expect "bestmove f7f5"
 
 send "ucinewgame\n"
 send "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -\n"
 send "go depth 18 searchmoves c6d7\n"
 expect "score mate 2 * pv c6d7 * f7f5"
 expect "bestmove c6d7"
 
 send "ucinewgame\n"
 send "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - - moves c6d7\n"
 send "go depth 18 searchmoves e3e2\n"
 expect "score mate -1 * pv e3e2 f7f5"
 expect "bestmove e3e2"

 send "setoption name EvalFile value verify.nnue\n"
 send "position startpos\n"
 send "go depth 5\n"
 expect "bestmove"

 send "setoption name MultiPV value 4\n"
 send "position startpos\n"
 send "go depth 5\n"
 expect "bestmove"

 send "setoption name Skill Level value 10\n"
 send "position startpos\n"
 send "go depth 5\n"
 expect "bestmove"
 send "setoption name Skill Level value 20\n"

 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for Valgrind
 lassign [wait] pid spawnid os_error_flag value
 exit \$value
EOF

#download TB as needed
if [ ! -d ../tests/syzygy ]; then
   curl -sL https://api.github.com/repos/niklasf/python-chess/tarball/9b9aa13f9f36d08aadfabff872882f4ab1494e95 | tar -xzf -
   mv niklasf-python-chess-9b9aa13 ../tests/syzygy
fi

cat << EOF > syzygy.exp
 set timeout 240
 # to correctly catch eof we need the following line
 # expect_before timeout { exit 2 } eof { exit 3 }
 expect_before timeout { exit 2 }
 spawn $exeprefix ./stockfish
 expect "Stockfish"
 send "uci\n"
 send "setoption name SyzygyPath value ../tests/syzygy/\n"
 expect "info string Found 35 WDL and 35 DTZ tablebase files (up to 4-man)."
 send "bench 128 1 8 default depth\n"
 expect "Nodes searched  :"
 send "ucinewgame\n"
 send "position fen 4k3/PP6/8/8/8/8/8/4K3 w - - 0 1\n"
 send "go depth 5\n"
 expect -re {score cp 20000|score mate}
 expect "bestmove"
 send "ucinewgame\n"
 send "position fen 8/1P6/2B5/8/4K3/8/6k1/8 w - - 0 1\n"
 send "go depth 5\n"
 expect -re {score cp 20000|score mate}
 expect "bestmove"
 send "ucinewgame\n"
 send "position fen 8/1P6/2B5/8/4K3/8/6k1/8 b - - 0 1\n"
 send "go depth 5\n"
 expect -re {score cp -20000|score mate}
 expect "bestmove"
 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for Valgrind
 lassign [wait] pid spawnid os_error_flag value
 exit \$value
EOF

for exp in game.exp syzygy.exp
do

  echo "======== $exp =============="
  cat $exp
  echo "============================"
  echo "$prefix expect $exp $postfix"
  eval "$prefix expect $exp $postfix"

  rm $exp

done

rm -f tsan.supp bench_tmp.epd

echo "instrumented testing OK"
