#!/bin/bash

cleanup() {

    echo "Cleaning up..."
    pkill -f "./WTP" 2>/dev/null
    sleep 1
    iw dev WTPWLan00 del 2>/dev/null
    echo "WTPWLan00 deleted!"
}

