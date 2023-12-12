#!/bin/bash

WORKING_DIR=/var/lib/scylla

echo "Working dir $WORKING_DIR"

rm -f ./file1.output
rm -f ./file2.output

./fsx $WORKING_DIR/file1 >> file1.output 2>&1 &
FSX1_PID=$!
echo "Running fsx #1 with PID $FSX1_PID and writing output at ./file1.output"
./fsx $WORKING_DIR/file2 >> file2.output 2>&1 &
FSX2_PID=$!
echo "Running fsx #2 with PID $FSX2_PID and writing output at ./file2.output"

run_fstrim() {
	echo "Running fstrim"
	while true; do sudo fstrim -v $WORKING_DIR >> fstrim_output; sleep 1; done
}

( run_fstrim ) &
FSTRIM_PID=$!

if wait -n $FSX1_PID $FSX2_PID; then
        echo "FSX succeeded"
else
        echo "FSX failed; inspect file*.output files in `pwd`"
fi
echo "Exit status: $?"

# do other stuff
kill -9 $FSX1_PID
kill -9 $FSX2_PID
kill -9 $FSTRIM_PID

