#!/usr/bin/env python3
"""
XOR SNN Verification Test

Tests all 4 XOR input combinations and verifies outputs.
"""

import requests
import json
import time
import sys

BASE_URL = "http://127.0.0.1:8000"

def check_emulator():
    """Check if emulator is running."""
    try:
        resp = requests.get(f"{BASE_URL}/api/emulator/status", timeout=2)
        return resp.status_code == 200
    except:
        return False

def deploy_xor():
    """Deploy XOR SNN."""
    print("=" * 60)
    print("DEPLOYING XOR SNN")
    print("=" * 60)
    
    import subprocess
    result = subprocess.run(
        ['./tools/nsnn', 'deploy', 'examples/xor_snn.json', '-c', '127.0.0.1'],
        capture_output=True,
        text=True
    )
    
    if "successfully" in result.stdout:
        print("✓ Deployment successful")
        return True
    else:
        print("✗ Deployment failed")
        print(result.stdout)
        return False

def start_snn():
    """Start SNN execution."""
    print("\n" + "=" * 60)
    print("STARTING SNN EXECUTION")
    print("=" * 60)
    
    resp = requests.post(f"{BASE_URL}/api/snn/start", json={})
    
    time.sleep(1)
    
    # Check activity
    resp = requests.get(f"{BASE_URL}/api/snn/activity")
    activity = resp.json()
    
    print(f"Engines initialized: {activity['total_engines']}")
    print(f"Total neurons: {activity['total_neurons']}")
    print(f"Routing active: {activity['routing_active']}")
    
    return activity['total_engines'] > 0

def test_xor_input(input_a, input_b, expected_output):
    """
    Test one XOR input combination.
    
    Args:
        input_a: Input A value (0 or 1)
        input_b: Input B value (0 or 1)
        expected_output: Expected XOR output (0 or 1)
    
    Returns:
        bool: True if test passed
    """
    print(f"\n{'='*60}")
    print(f"TEST: XOR({input_a}, {input_b}) = {expected_output}")
    print(f"{'='*60}")
    
    # Clear any previous spikes
    time.sleep(0.5)
    requests.get(f"{BASE_URL}/api/snn/events?count=1000")
    
    # Inject spikes for inputs
    # Neuron 0 = input A, Neuron 1 = input B
    spikes = []
    if input_a == 1:
        spikes.append({'neuron_id': 0, 'value': 1.0})
    if input_b == 1:
        spikes.append({'neuron_id': 1, 'value': 1.0})
    
    print(f"Injecting spikes: {spikes}")
    
    if spikes:
        resp = requests.post(f"{BASE_URL}/api/snn/input", json={'spikes': spikes})
        result = resp.json()
        print(f"Spikes injected: {result.get('spikes_injected', 0)}")
    else:
        print("No spikes to inject (both inputs are 0)")
    
    # Wait for propagation
    time.sleep(1.0)
    
    # Get spike events
    resp = requests.get(f"{BASE_URL}/api/snn/events?count=100")
    events = resp.json()
    
    print(f"\nTotal spike events: {events['count']}")
    
    # Check for output neuron firing (neuron 6 is the output)
    output_spikes = [s for s in events['spikes'] if s['neuron_id'] == 6]
    
    print(f"Output neuron (6) spikes: {len(output_spikes)}")
    
    if output_spikes:
        print(f"Output neuron fired at: {[s['timestamp_us'] for s in output_spikes]}")
    
    # Determine actual output
    actual_output = 1 if len(output_spikes) > 0 else 0
    
    # Verify
    passed = (actual_output == expected_output)
    
    print(f"\nExpected output: {expected_output}")
    print(f"Actual output:   {actual_output}")
    print(f"Result: {'✓ PASS' if passed else '✗ FAIL'}")
    
    # Show all neuron activity
    print(f"\nAll neuron activity:")
    neuron_activity = {}
    for spike in events['spikes']:
        nid = spike['neuron_id']
        neuron_activity[nid] = neuron_activity.get(nid, 0) + 1
    
    for nid in sorted(neuron_activity.keys()):
        count = neuron_activity[nid]
        neuron_type = "INPUT" if nid in [0, 1] else ("OUTPUT" if nid == 6 else "HIDDEN")
        print(f"  Neuron {nid} ({neuron_type}): {count} spikes")
    
    return passed

def main():
    """Main test function."""
    print("\n" + "=" * 60)
    print("XOR SNN VERIFICATION TEST")
    print("=" * 60)
    
    # Check emulator
    if not check_emulator():
        print("\n✗ ERROR: Emulator not running at", BASE_URL)
        print("Start it with: python3 z1_emulator.py")
        return 1
    
    print("✓ Emulator is running")
    
    # Deploy
    if not deploy_xor():
        return 1
    
    # Start
    if not start_snn():
        print("\n✗ ERROR: Failed to start SNN")
        return 1
    
    # Test all 4 XOR combinations
    # XOR truth table:
    # 0 XOR 0 = 0
    # 0 XOR 1 = 1
    # 1 XOR 0 = 1
    # 1 XOR 1 = 0
    
    tests = [
        (0, 0, 0),  # 0 XOR 0 = 0
        (0, 1, 1),  # 0 XOR 1 = 1
        (1, 0, 1),  # 1 XOR 0 = 1
        (1, 1, 0),  # 1 XOR 1 = 0
    ]
    
    results = []
    for input_a, input_b, expected in tests:
        passed = test_xor_input(input_a, input_b, expected)
        results.append((input_a, input_b, expected, passed))
    
    # Summary
    print("\n" + "=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)
    
    passed_count = sum(1 for _, _, _, passed in results if passed)
    total_count = len(results)
    
    for input_a, input_b, expected, passed in results:
        status = "✓ PASS" if passed else "✗ FAIL"
        print(f"XOR({input_a}, {input_b}) = {expected}: {status}")
    
    print(f"\nTotal: {passed_count}/{total_count} tests passed")
    
    if passed_count == total_count:
        print("\n🎉 ALL TESTS PASSED! XOR SNN is working correctly!")
        return 0
    else:
        print(f"\n⚠️  {total_count - passed_count} test(s) failed")
        return 1

if __name__ == "__main__":
    sys.exit(main())
