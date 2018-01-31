#!/bin/bash
# verify implementation of skill levels

error()
{
  echo "skill level testing failed on line $1"
  exit 1
}
trap 'error ${LINENO}' ERR

echo "skill level testing started"

# analyze start position with a given skill level
cat << EOF > skill.exp
 set timeout 10
 spawn ./stockfish
 lassign \$argv skill

 send "uci\n"
 expect "uciok"
 send "setoption name Skill level value \$skill\n"

 send "ucinewgame\n"
 send "position startpos\n"
 send "go depth 4\n"
 expect "bestmove"

 send "quit\n"
 expect eof
EOF

for i in 1 5
do 
touch skilllevel$i.exp
for k in `seq 1 30`
do
  expect skill.exp $i | grep bestmove | cut -c10-13 >> skilllevel$i.exp
done
numMoves=`sort skilllevel$i.exp | uniq -c | grep "[0-9]" -c`
if [ "$numMoves" != "4" ]; then
  exit 1  
fi
done

echo "skill level testing OK"

rm skill.exp
rm skilllevel*
