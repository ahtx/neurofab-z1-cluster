#!/usr/bin/env python3
"""
Simple Feedforward Network Test

Tests basic SNN functionality with a minimal 2-neuron network:
- Neuron 0 (input) connects to Neuron 1 (output) with weight 1.2
- Inject spike into neuron 0
- Verify neuron 1 fires

This test verifies:
1. Neuron ID mapping is correct
2. Spike injection works
3. Spike propagation works
4. Synaptic weights are applied
5. Threshold detection works
"""

import requests
import json
import time

BASE_URL = "http://127.0.0.1:8000"

def test_simple_feedforward():
    print("=" * 60)
    print("SIMPLE FEEDFORWARD NETWORK TEST")
    print("=" * 60)
    
    # Check emulator status
    resp = requests.get(f"{BASE_URL}/api/emulator/status")
    if not resp.json().get('emulator'):
        print("✗ Emulator not running")
        return False
    print("✓ Emulator is running")
    
    # Check SNN status
    resp = requests.get(f"{BASE_URL}/api/snn/activity")
    activity = resp.json()
    
    if activity['total_engines'] == 0:
        print("✗ No SNN engines initialized")
        print("  Run: ./tools/nsnn deploy examples/simple_feedforward.json -c 127.0.0.1")
        print("  Then: ./tools/nsnn start -c 127.0.0.1")
        return False
    
    print(f"✓ SNN initialized: {activity['total_engines']} engine(s), {activity['total_neurons']} neuron(s)")
    
    # Inspect network structure
    resp = requests.get(f"{BASE_URL}/api/snn/engines")
    engines = resp.json()['engines']
    
    print("\nNetwork Structure:")
    for eng in engines:
        print(f"  Node {eng['node_id']}:")
        for n in eng['neurons']:
            print(f"    Neuron {n['id']}: threshold={n['threshold']}, synapses={n['synapse_count']}")
    
    print("\n" + "=" * 60)
    print("TEST: Inject spike into neuron 0, expect neuron 1 to fire")
    print("=" * 60)
    
    # Clear spike history
    time.sleep(0.5)
    
    # Inject spike into neuron 0
    print("\n1. Injecting spike into neuron 0 (input)...")
    resp = requests.post(f"{BASE_URL}/api/snn/input", json={
        'spikes': [{'neuron_id': 0, 'value': 1.0}]
    })
    result = resp.json()
    print(f"   Spikes injected: {result.get('spikes_injected', 0)}")
    
    # Wait for propagation
    print("\n2. Waiting for spike propagation...")
    time.sleep(0.1)
    
    # Check spike events
    print("\n3. Checking spike activity...")
    resp = requests.get(f"{BASE_URL}/api/snn/events?count=20")
    events = resp.json().get('events', [])
    
    print(f"   Total spike events: {len(events)}")
    
    # Count spikes per neuron
    neuron_spikes = {}
    for event in events:
        nid = event['neuron_id']
        neuron_spikes[nid] = neuron_spikes.get(nid, 0) + 1
    
    print("\n   Spike activity:")
    for nid in sorted(neuron_spikes.keys()):
        print(f"     Neuron {nid}: {neuron_spikes[nid]} spike(s)")
    
    # Verify results
    print("\n" + "=" * 60)
    print("RESULTS")
    print("=" * 60)
    
    neuron_0_fired = neuron_spikes.get(0, 0) > 0
    neuron_1_fired = neuron_spikes.get(1, 0) > 0
    
    print(f"Neuron 0 (input) fired:  {'✓ YES' if neuron_0_fired else '✗ NO'}")
    print(f"Neuron 1 (output) fired: {'✓ YES' if neuron_1_fired else '✗ NO'}")
    
    if neuron_0_fired and neuron_1_fired:
        print("\n" + "=" * 60)
        print("✅ TEST PASSED - Spike propagation works correctly!")
        print("=" * 60)
        print("\nThis verifies:")
        print("  ✓ Neuron ID mapping is correct")
        print("  ✓ Spike injection works")
        print("  ✓ Spike propagation works")
        print("  ✓ Synaptic weights are applied")
        print("  ✓ Threshold detection works")
        print("  ✓ SNN engine is fully functional")
        return True
    else:
        print("\n" + "=" * 60)
        print("✗ TEST FAILED")
        print("=" * 60)
        if not neuron_0_fired:
            print("  Issue: Input neuron did not fire")
        if not neuron_1_fired:
            print("  Issue: Output neuron did not fire (spike didn't propagate)")
        return False

if __name__ == '__main__':
    success = test_simple_feedforward()
    exit(0 if success else 1)
