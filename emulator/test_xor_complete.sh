#!/bin/bash
# Complete XOR SNN Test Script
# Tests the full workflow: deploy, start, inject, monitor

set -e

echo "=========================================="
echo "Z1 Emulator - XOR SNN Complete Test"
echo "=========================================="
echo ""

# Check if emulator is running
if ! curl -s http://127.0.0.1:8000/api/emulator/status > /dev/null 2>&1; then
    echo "ERROR: Emulator not running at 127.0.0.1:8000"
    echo "Start it with: python3 z1_emulator.py"
    exit 1
fi

echo "✓ Emulator is running"
echo ""

# 1. Deploy SNN
echo "Step 1: Deploying XOR SNN..."
./tools/nsnn deploy examples/xor_snn.json -c 127.0.0.1
echo ""

# 2. Start SNN execution
echo "Step 2: Starting SNN execution..."
./tools/nsnn start -c 127.0.0.1
echo ""

# 3. Check SNN status
echo "Step 3: Checking SNN status..."
curl -s http://127.0.0.1:8000/api/snn/activity | python3 -m json.tool
echo ""

# 4. Test all XOR inputs
echo "Step 4: Testing XOR logic..."
echo ""

for input_file in examples/xor_input_*.json; do
    input_name=$(basename $input_file .json | sed 's/xor_input_//')
    echo "Testing input $input_name:"
    
    # Inject spikes
    ./tools/nsnn inject $input_file -c 127.0.0.1
    
    # Wait for processing
    sleep 0.5
    
    # Get spike events
    echo "  Spike events:"
    curl -s "http://127.0.0.1:8000/api/snn/events?count=10" | python3 -c "
import sys, json
data = json.load(sys.stdin)
if data['count'] > 0:
    for spike in data['spikes'][-5:]:
        print(f\"    Neuron {spike['neuron_id']} @ node {spike['node_id']} fired at {spike['timestamp_us']}us\")
else:
    print('    No spikes detected')
"
    echo ""
done

# 5. Final statistics
echo "Step 5: Final statistics..."
curl -s http://127.0.0.1:8000/api/snn/activity | python3 -m json.tool
echo ""

echo "=========================================="
echo "Test Complete!"
echo "=========================================="
