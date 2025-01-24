#!/bin/bash

trap 'cleanup' INT TERM

cleanup() {
    echo "Shutting down..."
    kill -TERM 0  # Send TERM signal to all processes in the current process group
    wait            # Wait for all child processes to terminate
    echo "Done."
}

bin/sample_client localhost IBM &
bin/sample_client localhost FB &
bin/sample_client localhost GOOG &
bin/sample_client localhost AMZN &
bin/sample_client localhost ORCL &
bin/sample_client localhost NFLX &
bin/sample_client localhost AAPL &

wait
