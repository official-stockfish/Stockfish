#!/bin/bash
# verify reproducible search

error()
{
  echo "reprosearch testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

echo "reprosearch testing started"

# repeat two short games, separated by ucinewgame.
# with go nodes $nodes they should result in exactly
# the same node count for each iteration.
cat << EOF > repeat.exp
 set timeout 10
 spawn ./stockfish
 lassign \$argv nodes

 send "uci\n"
 expect "uciok"

 send "ucinewgame\n"
 send "position startpos\n"
 send "go nodes \$nodes\n"
 expect "bestmove"

 send "position startpos moves e2e4 e7e6\n"
 send "go nodes \$nodes\n"
 expect "bestmove"

 send "ucinewgame\n"
 send "position startpos\n"
 send "go nodes \$nodes\n"
 expect "bestmove"

 send "position startpos moves e2e4 e7e6\n"
 send "go nodes \$nodes\n"
 expect "bestmove"

 send "quit\n"
 expect eof
EOF

# to increase the likelihood of finding a non-reproducible case,
# the allowed number of nodes are varied systematically
for i in `seq 1 20`
do

  nodes=$((100*3**i/2**i))
  echo "reprosearch testing with $nodes nodes"

  # each line should appear exactly an even number of times
  expect repeat.exp $nodes 2>&1 | grep -o "nodes [0-9]*" | sort | uniq -c | awk '{if ($1%2!=0) exit(1)}'

done

rm repeat.exp

echo "reprosearch testing OK"
