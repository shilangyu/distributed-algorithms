#!/bin/bash

set -e

BUILD_TYPE=Release ./build.sh &> /dev/null

MESSAGES=100
PROCESSES=100
RUN_FOLDER=$(mktemp -d)
HOSTS_FILE=$RUN_FOLDER/hosts
OUTPUTS=$RUN_FOLDER/outputs
CONFIG_FILE=$RUN_FOLDER/fifo-broadcast.config
SLEEP_TIME_S=3

mkdir $OUTPUTS


echo "Data stored in "$RUN_FOLDER


# create hosts file
for i in $(seq 1 $PROCESSES)
do
	echo $i" localhost "$((11000 + $i)) >> $HOSTS_FILE
done

# create config file
echo $MESSAGES > $CONFIG_FILE


# start all processes
PIDS=()
for i in $(seq 1 $PROCESSES)
do
	./bin/da_proc --id $i --hosts $HOSTS_FILE --output $OUTPUTS/$i.out $CONFIG_FILE 1> $OUTPUTS/$i.stdout 2> $OUTPUTS/$i.stderr &
	PIDS+=($!)
done


# give processes some time to run
sleep $SLEEP_TIME_S

# send SIGTERM to all
for key in "${!PIDS[@]}"
do
	pid=${PIDS[$key]}
  kill -s TERM $pid
	wait $pid || echo "Process "$(($key + 1))" exited with "$?
done

delivered=$(cat $OUTPUTS/*.out | grep '^d' | wc -l | xargs)
total=$(($PROCESSES * $PROCESSES * $MESSAGES))
frac=$(echo "$delivered/$total * 100" | bc -l)

echo "Delivered "$frac"%"
