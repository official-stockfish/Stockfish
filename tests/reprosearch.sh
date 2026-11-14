#!/bin/bash
# verify reproducible search

set -o pipefail

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
 expect_after {
   timeout { puts stderr "Timed out waiting for Stockfish"; exit 1 }
   eof { puts stderr "Stockfish exited unexpectedly"; exit 1 }
 }

 send "uci\n"
 expect "uciok"

 send "ucinewgame\n"
 send "position startpos\n"
 puts "search 1"
 send "go nodes \$nodes\n"
 expect "bestmove"

 send "position startpos moves e2e4 e7e6\n"
 puts "search 2"
 send "go nodes \$nodes\n"
 expect "bestmove"

 send "ucinewgame\n"
 send "position startpos\n"
 puts "search 3"
 send "go nodes \$nodes\n"
 expect "bestmove"

 send "position startpos moves e2e4 e7e6\n"
 puts "search 4"
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

  # Compare the ordered node counts from each position with its repeat.
  expect repeat.exp "$nodes" | awk '
    /^search [1-4]/ { search = $2 }
    /^info / && match($0, /nodes [0-9]+/) {
      counts[search] = counts[search] " " substr($0, RSTART, RLENGTH)
    }
    END {
      if (search != 4 || !counts[1] || !counts[2] ||
          counts[1] != counts[3] || counts[2] != counts[4])
        exit 1
    }'

done

rm repeat.exp

echo "reprosearch testing OK"
