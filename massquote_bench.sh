#!/bin/bash

HOST=localhost
COUNT=100

if [ "$#" -eq 1 ]; then
    HOST=$1
fi
if [ "$#" -eq 2 ]; then
    COUNT=$2
fi

trap 'cleanup' INT TERM

cleanup() {
    echo "Shutting down..."
    kill -TERM 0  # Send TERM signal to all processes in the current process group
    wait            # Wait for all child processes to terminate
    echo "Done."
}

for i in $(seq 1 $COUNT); do
    bin/sample_client $HOST S$i &
done

wait
