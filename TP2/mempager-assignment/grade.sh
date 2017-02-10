#!/bin/bash
set -u

make

cat mempager-tests/tests.spec | while read num frames blocks nodiff ; do
    echo "running test$num"
    ./bin/mmu $frames $blocks &> test$num.mmu.out &
    sleep 1s
    ./bin/test$num &> test$num.out
    kill -SIGINT %1
    wait
    rm -rf mmu.sock mmu.pmem.img.*
    if [ $nodiff -eq 1 ] ; then
        continue
    fi
    if ! diff mempager-tests/test$num.mmu.out test$num.mmu.out > /dev/null ; then
        echo "test$num.mmu.out differs"
    fi
    if ! diff mempager-tests/test$num.out test$num.out > /dev/null ; then
        echo "test$num.out differs"
    fi
done
