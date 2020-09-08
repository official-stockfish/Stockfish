#!/bin/bash
# verify perft numbers (positions from www.chessprogramming.org/Perft_Results)

error()
{
  echo "perft testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

echo "perft testing started"

cat << EOF > perft.exp
   set timeout 10
   lassign \$argv pos depth result
   spawn ./stockfish
   send "position \$pos\\ngo perft \$depth\\n"
   expect "Nodes searched? \$result" {} timeout {exit 1}
   send "quit\\n"
   expect eof
EOF

expect perft.exp startpos 5 4865609 > /dev/null
expect perft.exp "fen r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -" 5 193690690 > /dev/null
expect perft.exp "fen 8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -" 6 11030083 > /dev/null
expect perft.exp "fen r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1" 5 15833292 > /dev/null
expect perft.exp "fen rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8" 5 89941194 > /dev/null
expect perft.exp "fen r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10" 5 164075551 > /dev/null

rm perft.exp

echo "perft testing OK"
