CC = gcc
COMPILE_FLAGS = -g -Wall -std=c99

all: bin
	$(CC) $(COMPILE_FLAGS) -c fs.c
#	gcc -g -Wall testPadronizado.c fs.c -o bin/fs
#	$(CC) $(COMPILE_FLAGS) -I. anotherTests/test1.c fs.o -o bin/test1
#	bin/test1
#	$(CC) $(COMPILE_FLAGS) -I. anotherTests/test1.c fs.o -o bin/test2
#	bin/test2

grade:
	./grade.sh

shell: bin
	$(CC) $(COMPILE_FLAGS) -c fs.c 
	$(CC) $(COMPILE_FLAGS) -I. shell.c fs.o -o bin/shell
	bin/shell

run: bin
	bin/fs

test: bin 
	$(CC) $(COMPILE_FLAGS) -c fs.c 
	$(CC) $(COMPILE_FLAGS) -I. test.c fs.o -o bin/test
	bin/test 0 24 1024
	bin/test 1
	bin/test 2
	diff files/hosts extract/hosts
	diff files/bin/bash extract/bin/bash
	diff files/home/cassios/addonsu-arm-signed extract/home/cassios/addonsu-arm-signed
	diff files/home/yuri/dhclientconf extract/home/yuri/dhclientconf

gdb:
	gdb bin/test

bin: 
	mkdir bin

clean:
	rm -rf bin/
	rm img
