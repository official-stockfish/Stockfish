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
    exeprefix='valgrind --error-exitcode=42'
    postfix='1>/dev/null'
  ;;
  --sanitizer)
    echo "sanitizer testing started"
    prefix='!'
    exeprefix=''
    postfix='2>&1 | grep "runtime error:"'
  ;;
  *)
    echo "unknown testing started"
    prefix=''
    exeprefix=''
    postfix=''
  ;;
esac

# simple command line testing
for args in "eval" \
            "go nodes 1000" \
            "go depth 10" \
            "go movetime 1000" \
            "go wtime 8000 btime 8000 winc 500 binc 500" \
            "bench 128 1 10 default depth"
do

   echo "$prefix $exeprefix ./stockfish $args $postfix"
   eval "$prefix $exeprefix ./stockfish $args $postfix"

done

# more general testing, following an uci protocol exchange
cat << EOF > game.exp
 set timeout 10
 spawn $exeprefix ./stockfish

 send "uci\n"
 expect "uciok"

 send "ucinewgame\n"
 send "position startpos\n"
 send "go nodes 1000\n"
 expect "bestmove"

 send "position startpos moves e2e4 e7e6\n"
 send "go nodes 1000\n"
 expect "bestmove"

 send "position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1\n"
 send "go depth 30\n"
 expect "bestmove"

 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for valgrind
 lassign [wait] pid spawnid os_error_flag value
 exit \$value
EOF

cat << EOF > syzygy.exp
 set timeout 240
 spawn $exeprefix ./stockfish
 send "uci\n"
 send "setoption name SyzygyPath value ../tests/data/syzygy/\n"
 send "bench 128 1 10 default depth\n"
 # root in TB
 send "position fen 4k3/R7/8/8/8/8/n7/5K2 w - - 0 1\n"
 send "go nodes 1000\n"
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

echo "instrumented testing OK"
