#!/bin/bash
echo $(uname -a)

if [ "$#" -ne 1 ]; then
    echo "Please give port where echo server is running: $0 [port]"
    exit
fi

PID=$(lsof -itcp:$1 | sed -n -e 2p | awk '{print $2}')
taskset -cp 0 $PID

for bytes in 1 128 512 1000
do
	for connections in 1 50 150 300 500 1000
	do
   	cargo run --release -- --address "localhost:$1" --number $connections --duration 60 --length $bytes
   	sleep 4
	done
done
