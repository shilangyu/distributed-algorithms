#!/bin/sh

set -e

./build.sh &> /dev/null

MESSAGES=100
PROCESSES=100
RUN_FOLDER=$(mktemp -d)
HOSTS_FILE=$RUN_FOLDER/hosts
OUTPUTS=$RUN_FOLDER/outputs
CONFIG_FILE=$RUN_FOLDER/perfect-links.config

mkdir $OUTPUTS


echo "Data stored in "$RUN_FOLDER


# create hosts file
for i in $(seq 1 $PROCESSES)
do
	echo $i" localhost "$((11000 + $i)) >> $HOSTS_FILE
done

# create config file
echo $MESSAGES" 1" > $CONFIG_FILE


# start 100 processes
PIDS=()
for i in $(seq 1 $PROCESSES)
do
	./bin/da_proc --id $i --hosts $HOSTS_FILE --output $OUTPUTS/$i.out $CONFIG_FILE 1> $OUTPUTS/$i.stdout 2> $OUTPUTS/$i.stderr &
	PIDS+=($!)
done


# give processes 3 seconds
sleep 3

# send SIGTERM to all
for pid in "${PIDS[@]}"
do
  kill -s TERM $pid
	wait $pid || true
	# TODO: collect exit codes
done

delivered=$(cat $OUTPUTS/1.out | wc -l | xargs)
total=$((($PROCESSES - 1) * $MESSAGES))
frac=$(echo "$delivered/$total * 100" | bc -l)

echo "Delivered "$frac"%"
