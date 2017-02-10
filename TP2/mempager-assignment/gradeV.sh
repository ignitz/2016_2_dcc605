#!/bin/bash
set -u

make

cat mempager-tests/tests.spec | while read num frames blocks nodiff ; do
    echo "running test$num"
    ./bin/mmu $frames $blocks &
    sleep 1s
    ./bin/test$num &> test$num.out
    kill -SIGINT %1
    wait
    rm -rf mmu.sock mmu.pmem.img.*
done
