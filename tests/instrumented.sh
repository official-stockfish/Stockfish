#!/bin/bash
# check for errors under Valgrind or sanitizers.

error()
{
  echo "instrumented testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

# Since Linux Kernel 6.5 we are getting false positives from the ci,
# lower the ALSR entropy to disable ALSR, which works as a temporary workaround.
# https://github.com/google/sanitizers/issues/1716
# https://bugs.launchpad.net/ubuntu/+source/linux/+bug/2056762
sudo sysctl -w vm.mmap_rnd_bits=28


# define suitable post and prefixes for testing options
case $1 in
  --valgrind)
    echo "valgrind testing started"
    prefix=''
    exeprefix='valgrind --error-exitcode=42 --errors-for-leak-kinds=all --leak-check=full'
    postfix='1>/dev/null'
    threads="1"
  ;;
  --valgrind-thread)
    echo "valgrind-thread testing started"
    prefix=''
    exeprefix='valgrind --fair-sched=try --error-exitcode=42'
    postfix='1>/dev/null'
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
race:Stockfish::TTEntry::move
race:Stockfish::TTEntry::depth
race:Stockfish::TTEntry::bound
race:Stockfish::TTEntry::save
race:Stockfish::TTEntry::value
race:Stockfish::TTEntry::eval
race:Stockfish::TTEntry::is_pv

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
 spawn $exeprefix ./stockfish

 send "uci\n"
 expect "uciok"

 # send "setoption name Debug Log File value debug.log\n"
 send "setoption name Threads value $threads\n"

 send "ucinewgame\n"
 send "position startpos\n"
 send "go nodes 1000\n"
 expect "bestmove"

 send "position startpos moves e2e4 e7e6\n"
 send "go nodes 1000\n"
 expect "bestmove"

 send "position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1\n"
 send "go depth 10\n"
 expect "bestmove"

 send "setoption name UCI_ShowWDL value true\n"
 send "position startpos\n"
 send "flip\n"
 send "go depth 5\n"
 expect "bestmove"

 send "setoption name Skill Level value 10\n"
 send "position startpos\n"
 send "go depth 5\n"
 expect "bestmove"

 send "setoption name Clear Hash\n"

 send "setoption name EvalFile value verify.nnue\n"
 send "position startpos\n"
 send "go depth 5\n"
 expect "bestmove"

 send "setoption name MultiPV value 4\n"
 send "position startpos\n"
 send "go depth 5\n"

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
 spawn $exeprefix ./stockfish
 send "uci\n"
 send "setoption name SyzygyPath value ../tests/syzygy/\n"
 expect "info string Found 35 tablebases" {} timeout {exit 1}
 send "bench 128 1 8 default depth\n"
 send "ucinewgame\n"
 send "position fen 4k3/PP6/8/8/8/8/8/4K3 w - - 0 1\n"
 send "go depth 5\n"
 expect "bestmove"
 send "position fen 8/1P6/2B5/8/4K3/8/6k1/8 w - - 0 1\n"
 send "go depth 5\n"
 expect "bestmove"
 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for Valgrind
 lassign [wait] pid spawnid os_error_flag value
 exit \$value
EOF

for exp in game.exp syzygy.exp
do

  echo "$prefix expect $exp $postfix"
  eval "$prefix expect $exp $postfix"

  rm $exp

done

rm -f tsan.supp bench_tmp.epd

echo "instrumented testing OK"
