#!/bin/bash

set -e

BUILD_TYPE=Release ./build.sh &> /dev/null

AGREEMENT_COUNT=10
PROPOSE_AMOUNT=3
MAX_UNIQUE_PROPOSED=5
MAX_PROPOSED_VALUE=2147483648
PROCESSES=10
RUN_FOLDER=$(mktemp -d)
HOSTS_FILE=$RUN_FOLDER/hosts
OUTPUTS=$RUN_FOLDER/outputs
CONFIG_FILE=$RUN_FOLDER/lattice-agreement.config
SLEEP_TIME_S=30

mkdir $OUTPUTS


echo "Data stored in "$RUN_FOLDER


# create hosts file
for i in $(seq 1 $PROCESSES)
do
	echo $i" localhost "$((11000 + $i)) >> $HOSTS_FILE
done

# pick random values
NUMS=()
for i in $(seq 1 $MAX_UNIQUE_PROPOSED)
do
	num=$(shuf -i 1-$MAX_PROPOSED_VALUE -n 1)
	NUMS+=($num)
done

# create config files
for i in $(seq 1 $PROCESSES)
do
	echo $AGREEMENT_COUNT" "$PROPOSE_AMOUNT" "$MAX_UNIQUE_PROPOSED > $CONFIG_FILE.$i
	
	for j in $(seq 1 $AGREEMENT_COUNT)
	do
		NUM=()
		for k in $(shuf -i 0-$(($MAX_UNIQUE_PROPOSED-1)) -n $(shuf -i 1-$PROPOSE_AMOUNT -n 1))
		do
			NUM+=(${NUMS[$k]})
		done

		echo ${NUM[@]} >> $CONFIG_FILE.$i
	done
done

# start all processes
PIDS=()
for i in $(seq 1 $PROCESSES)
do
	./bin/da_proc --id $i --hosts $HOSTS_FILE --output $OUTPUTS/$i.out $CONFIG_FILE.$i 1> $OUTPUTS/$i.stdout 2> $OUTPUTS/$i.stderr &
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

./tools/validate_lattice_agreement.py --configs $CONFIG_FILE.* --outputs $OUTPUTS/*.out

delivered=$(cat $OUTPUTS/*.out | wc -l | xargs)
total=$(($PROCESSES * $AGREEMENT_COUNT))
frac=$(echo "$delivered/$total * 100" | bc -l)

echo "Delivered "$frac"%"
