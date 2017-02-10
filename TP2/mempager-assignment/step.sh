#!/bin/bash
set -u

declare -u frames
declare -u blocks
declare -u nodiff

frames=4
blocks=8
nodiff=0

for num in `seq 0 0`;
do
  echo $num $frames $blocks $nodiff
  # ./bin/mmu $frames $blocks &> test$num.mmu.out &
  ./bin/mmu $frames $blocks &
  sleep 1s
  # ./bin/test$num &> test$num.out
  ./bin/test$num
  kill -SIGINT %1
  wait
  rm -rf mmu.sock mmu.pmem.img.*

done
