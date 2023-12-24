#!/bin/bash

set -e

./build.sh &> /dev/null

AGREEMENT_COUNT=100000
PROPOSE_AMOUNT=10
MAX_UNIQUE_PROPOSED=30
MAX_PROPOSED_VALUE=2147483648
PROCESSES=10
RUN_FOLDER=$(mktemp -d)
HOSTS_FILE=$RUN_FOLDER/hosts
OUTPUTS=$RUN_FOLDER/outputs
CONFIG_FILE=$RUN_FOLDER/lattice-agreement.config
SLEEP_TIME_S=10

mkdir $OUTPUTS


echo "Data stored in "$RUN_FOLDER


# create hosts file
for i in $(seq 1 $PROCESSES)
do
	echo $i" localhost "$((11000 + $i)) >> $HOSTS_FILE
done

# create config files
CONFIG_FILES=()
for i in $(seq 1 $PROCESSES)
do
	CONFIG_FILES+=("$CONFIG_FILE.$i")
done
./tools/generate_lattice_agreement_config.py --p $AGREEMENT_COUNT --vs $PROPOSE_AMOUNT --ds $MAX_UNIQUE_PROPOSED --config-files ${CONFIG_FILES[@]}

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

chmod -R 777 $RUN_FOLDER

delivered=$(cat $OUTPUTS/*.out | wc -l | xargs)
total=$(($PROCESSES * $AGREEMENT_COUNT))
frac=$(echo "$delivered/$total * 100" | bc -l)

echo "Delivered "$frac"%"
