#!/bin/bash
#
# Quick test script for Z1 emulator
#
# This script tests basic emulator functionality.
#

set -e  # Exit on error

EMULATOR_IP="127.0.0.1"
EMULATOR_PORT="8000"
TOOLS_DIR="../tools"

echo "=========================================="
echo "Z1 Emulator Quick Test"
echo "=========================================="
echo ""

# Check if emulator is running
echo "[1/6] Checking if emulator is running..."
if curl -s "http://${EMULATOR_IP}:${EMULATOR_PORT}/api/emulator/status" > /dev/null; then
    echo "✓ Emulator is running"
else
    echo "✗ Emulator is not running!"
    echo ""
    echo "Please start the emulator first:"
    echo "  cd .."
    echo "  python3 z1_emulator.py"
    exit 1
fi
echo ""

# Test node listing
echo "[2/6] Testing node listing..."
${TOOLS_DIR}/nls -c ${EMULATOR_IP} > /dev/null
if [ $? -eq 0 ]; then
    echo "✓ Node listing works"
else
    echo "✗ Node listing failed"
    exit 1
fi
echo ""

# Test node ping
echo "[3/6] Testing node ping..."
${TOOLS_DIR}/nping 0 -c ${EMULATOR_IP} -n 1 > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ Node ping works"
else
    echo "✗ Node ping failed"
    exit 1
fi
echo ""

# Test memory operations
echo "[4/6] Testing memory operations..."
echo "Test data" > /tmp/test_data.bin
${TOOLS_DIR}/ncp /tmp/test_data.bin 0@0x20000000 -c ${EMULATOR_IP} > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ Memory write works"
else
    echo "✗ Memory write failed"
    exit 1
fi
rm -f /tmp/test_data.bin
echo ""

# Test SNN deployment
echo "[5/6] Testing SNN deployment..."
${TOOLS_DIR}/nsnn deploy xor_snn.json -c ${EMULATOR_IP} > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ SNN deployment works"
else
    echo "✗ SNN deployment failed"
    exit 1
fi
echo ""

# Test SNN start/stop
echo "[6/6] Testing SNN execution..."
${TOOLS_DIR}/nsnn start -c ${EMULATOR_IP} > /dev/null 2>&1
sleep 1
${TOOLS_DIR}/nsnn stop -c ${EMULATOR_IP} > /dev/null 2>&1
if [ $? -eq 0 ]; then
    echo "✓ SNN execution works"
else
    echo "✗ SNN execution failed"
    exit 1
fi
echo ""

echo "=========================================="
echo "All tests passed! ✓"
echo "=========================================="
echo ""
echo "The emulator is working correctly."
echo "You can now:"
echo "  - Deploy SNNs with: ${TOOLS_DIR}/nsnn deploy <topology.json>"
echo "  - Monitor activity with: ${TOOLS_DIR}/nsnn monitor <duration_ms>"
echo "  - Flash firmware with: ${TOOLS_DIR}/nflash flash <firmware.bin> <node>"
echo ""
