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
race:TTEntry::move
race:TTEntry::depth
race:TTEntry::bound
race:TTEntry::save
race:TTEntry::value
race:TTEntry::eval
race:TTEntry::is_pv

race:TranspositionTable::probe
race:TranspositionTable::hashfull

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

mkdir -p training_data
mkdir -p validation_data

# gensfen testing 01
cat << EOF > gensfen01.exp
 set timeout 240
 spawn $exeprefix ./stockfish

 send "uci\n"
 expect "uciok"

 send "setoption name Threads value $threads\n"
 send "setoption name Use NNUE value false\n"
 send "isready\n"
 send "gensfen depth 3 loop 100 use_draw_in_training_data_generation 1 eval_limit 32000 output_file_name training_data/training_data.bin sfen_format bin\n"
 expect "INFO: Gensfen finished."
 send "convert_plain targetfile training_data/training_data.bin output_file_name training_data.txt\n"
 expect "all done"
 send "gensfen depth 3 loop 100 use_draw_in_training_data_generation 1 eval_limit 32000 output_file_name training_data/training_data.binpack sfen_format binpack\n"
 expect "INFO: Gensfen finished."

 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for valgrind
 lassign [wait] pid spawnid os_error_flag value
 exit \$value
EOF

# gensfen testing 02
cat << EOF > gensfen02.exp
 set timeout 240
 spawn $exeprefix ./stockfish

 send "uci\n"
 expect "uciok"

 send "setoption name Threads value $threads\n"
 send "setoption name Use NNUE value true\n"
 send "isready\n"
 send "gensfen depth 4 loop 50 use_draw_in_training_data_generation 1 eval_limit 32000 output_file_name validation_data/validation_data.bin sfen_format bin\n"
 expect "INFO: Gensfen finished."
 send "gensfen depth 4 loop 50 use_draw_in_training_data_generation 1 eval_limit 32000 output_file_name validation_data/validation_data.binpack sfen_format binpack\n"
 expect "INFO: Gensfen finished."

 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for valgrind
 lassign [wait] pid spawnid os_error_flag value
 exit \$value
EOF

# simple learning
cat << EOF > learn01.exp
 set timeout 240
 spawn $exeprefix ./stockfish

 send "uci\n"
 send "setoption name SkipLoadingEval value true\n"
 send "setoption name Use NNUE value true\n"
 send "setoption name Threads value $threads\n"
 send "isready\n"
 send "learn targetdir training_data epochs 1 sfen_read_size 100 thread_buffer_size 10 batchsize 100 use_draw_in_training 1 use_draw_in_validation 1 lr 1 eval_limit 32000 nn_batch_size 30 newbob_decay 0.5 eval_save_interval 30 loss_output_interval 10 validation_set_file_name validation_data/validation_data.bin\n"

 expect "INFO (save_eval): Finished saving evaluation file in evalsave/final"

 send "quit\n"
 expect eof

 # return error code of the spawned program, useful for valgrind
 lassign [wait] pid spawnid os_error_flag value
 exit \$value

EOF

for exp in gensfen01.exp gensfen02.exp learn01.exp
do

  echo "$prefix expect $exp $postfix"
  eval "$prefix expect $exp $postfix"

  rm $exp

done

rm -f tsan.supp

echo "instrumented learn testing OK"
