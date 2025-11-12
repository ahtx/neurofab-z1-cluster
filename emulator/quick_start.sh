#!/bin/bash
#
# Quick Start Script for Z1 Emulator
#
# This script demonstrates the complete workflow with the emulator.
#

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

echo "=========================================="
echo "Z1 Emulator Quick Start"
echo "=========================================="
echo ""

# Check if emulator is running
echo "[1/5] Checking emulator status..."
if curl -s http://127.0.0.1:8000/api/emulator/status > /dev/null 2>&1; then
    echo "✓ Emulator is already running"
else
    echo "✗ Emulator is not running"
    echo ""
    echo "Starting emulator in background..."
    cd "${SCRIPT_DIR}"
    python3 z1_emulator.py > /tmp/z1_emulator.log 2>&1 &
    EMULATOR_PID=$!
    echo "  PID: ${EMULATOR_PID}"
    echo "  Log: /tmp/z1_emulator.log"
    
    # Wait for emulator to start
    echo "  Waiting for emulator to start..."
    for i in {1..10}; do
        if curl -s http://127.0.0.1:8000/api/emulator/status > /dev/null 2>&1; then
            echo "✓ Emulator started successfully"
            break
        fi
        sleep 1
    done
fi
echo ""

# Set environment
echo "[2/5] Setting environment variables..."
export Z1_CONTROLLER_IP=127.0.0.1
export Z1_CONTROLLER_PORT=8000
echo "✓ Z1_CONTROLLER_IP=${Z1_CONTROLLER_IP}"
echo "✓ Z1_CONTROLLER_PORT=${Z1_CONTROLLER_PORT}"
echo ""

# List nodes
echo "[3/5] Listing cluster nodes..."
"${SCRIPT_DIR}/tools/nls" -c 127.0.0.1
echo ""

# Deploy XOR SNN
echo "[4/5] Deploying XOR SNN..."
"${SCRIPT_DIR}/tools/nsnn" deploy "${SCRIPT_DIR}/examples/xor_snn.json" -c 127.0.0.1
echo "✓ XOR network deployed"
echo ""

# Start SNN
echo "[5/5] Starting SNN execution..."
"${SCRIPT_DIR}/tools/nsnn" start -c 127.0.0.1
echo "✓ SNN execution started"
echo ""

echo "=========================================="
echo "Quick Start Complete!"
echo "=========================================="
echo ""
echo "The emulator is running with XOR SNN deployed."
echo ""
echo "Try these commands:"
echo "  # Inject input spikes"
echo "  ./tools/nsnn inject examples/xor_input_01.json -c 127.0.0.1"
echo ""
echo "  # Monitor activity"
echo "  ./tools/nsnn monitor 1000 -c 127.0.0.1"
echo ""
echo "  # Check cluster status"
echo "  ./tools/nstat -c 127.0.0.1"
echo ""
echo "  # Stop SNN"
echo "  ./tools/nsnn stop -c 127.0.0.1"
echo ""
echo "To stop the emulator:"
echo "  curl -X POST http://127.0.0.1:8000/api/emulator/reset"
echo "  kill ${EMULATOR_PID:-\$(pgrep -f z1_emulator.py)}"
echo ""
