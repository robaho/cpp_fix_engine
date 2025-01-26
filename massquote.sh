#!/bin/bash

HOST=localhost

if [ "$#" -eq 1 ]; then
    HOST=$1
fi

trap 'cleanup' INT TERM

cleanup() {
    echo "Shutting down..."
    kill -TERM 0  # Send TERM signal to all processes in the current process group
    wait            # Wait for all child processes to terminate
    echo "Done."
}

bin/sample_client $HOST IBM &
bin/sample_client $HOST FB &
bin/sample_client $HOST GOOG &
bin/sample_client $HOST AMZN &
bin/sample_client $HOST ORCL &
bin/sample_client $HOST NFLX &
bin/sample_client $HOST AAPL &

wait
