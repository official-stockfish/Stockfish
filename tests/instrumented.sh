#!/bin/bash
# check for errors under valgrind or sanitizers.

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

# simple command line testing
for args in "eval" \
            "go nodes 1000" \
            "go depth 10" \
            "go movetime 1000" \
            "go wtime 8000 btime 8000 winc 500 binc 500" \
            "bench 128 $threads 8 default depth"
do

   echo "$prefix $exeprefix ./stockfish $args $postfix"
   eval "$prefix $exeprefix ./stockfish $args $postfix"

done

# more general testing, following an uci protocol exchange
cat << EOF > game.exp
 set timeout 240
 spawn $exeprefix ./stockfish

 send "uci\n"
 expect "uciok"

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

 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for valgrind
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
 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for valgrind
 lassign [wait] pid spawnid os_error_flag value
 exit \$value
EOF

for exp in game.exp syzygy.exp
do

  echo "$prefix expect $exp $postfix"
  eval "$prefix expect $exp $postfix"

  rm $exp

done

rm -f tsan.supp

echo "instrumented testing OK"
