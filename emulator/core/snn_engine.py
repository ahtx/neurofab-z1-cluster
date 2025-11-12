"""
SNN Execution Engine Emulator

Simulates Leaky Integrate-and-Fire (LIF) neurons and spike propagation.
"""

import time
import struct
import threading
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from collections import deque


@dataclass
class Neuron:
    """LIF neuron state."""
    neuron_id: int
    membrane_potential: float = 0.0
    threshold: float = 1.0
    leak_rate: float = 0.95
    refractory_period_us: int = 1000
    last_spike_time_us: int = 0
    synapse_count: int = 0
    synapse_table_addr: int = 0
    flags: int = 0


@dataclass
class Synapse:
    """Synapse connection."""
    source_neuron_global_id: int  # (backplane_id << 24) | (node_id << 16) | local_id
    weight: float
    delay_us: int = 1000


@dataclass
class Spike:
    """Spike event."""
    neuron_id: int
    source_node: int
    source_backplane: int
    timestamp_us: int
    value: float = 1.0


class SNNEngine:
    """Simulates SNN execution on a compute node."""
    
    def __init__(self, node_id: int, backplane_id: int):
        """
        Initialize SNN engine.
        
        Args:
            node_id: Node ID
            backplane_id: Backplane ID
        """
        self.node_id = node_id
        self.backplane_id = backplane_id
        
        # Neuron table
        self.neurons: Dict[int, Neuron] = {}
        self.synapses: Dict[int, List[Synapse]] = {}  # neuron_id -> list of synapses
        
        # Spike queues
        self.incoming_spikes: deque = deque()
        self.outgoing_spikes: deque = deque()
        
        # Simulation state
        self.running = False
        self.current_time_us = 0
        self.timestep_us = 1000  # 1ms default
        
        # Statistics
        self.stats = {
            'total_spikes_received': 0,
            'total_spikes_sent': 0,
            'neurons_spiked': 0,
            'simulation_steps': 0
        }
        
        # Execution thread
        self.exec_thread: Optional[threading.Thread] = None
    
    def load_neuron_table(self, neuron_table_data: bytes):
        """
        Load neuron table from binary data.
        
        Each neuron entry is 256 bytes:
        - Bytes 0-3: Flags (uint32)
        - Bytes 4-7: Membrane potential (float32)
        - Bytes 8-11: Threshold (float32)
        - Bytes 12-15: Leak rate (float32)
        - Bytes 16-19: Refractory period (uint32 microseconds)
        - Bytes 20-23: Last spike time (uint32 microseconds)
        - Bytes 24-27: Synapse count (uint32)
        - Bytes 28-31: Synapse table address (uint32)
        - Bytes 32-255: Reserved
        
        Args:
            neuron_table_data: Binary neuron table data
        """
        entry_size = 256
        num_neurons = len(neuron_table_data) // entry_size
        
        self.neurons.clear()
        
        for i in range(num_neurons):
            offset = i * entry_size
            entry = neuron_table_data[offset:offset+entry_size]
            
            # Parse neuron entry
            flags, v_mem, threshold, leak_rate = struct.unpack_from('<If3f', entry, 0)
            refrac, last_spike, syn_count, syn_addr = struct.unpack_from('<4I', entry, 16)
            
            neuron = Neuron(
                neuron_id=i,
                membrane_potential=v_mem,
                threshold=threshold,
                leak_rate=leak_rate,
                refractory_period_us=refrac,
                last_spike_time_us=last_spike,
                synapse_count=syn_count,
                synapse_table_addr=syn_addr,
                flags=flags
            )
            
            self.neurons[i] = neuron
    
    def load_synapse_table(self, neuron_id: int, synapse_data: bytes):
        """
        Load synapse table for a neuron.
        
        Each synapse entry is 12 bytes:
        - Bytes 0-3: Source neuron global ID (uint32)
        - Bytes 4-7: Weight (float32)
        - Bytes 8-11: Delay (uint32 microseconds)
        
        Args:
            neuron_id: Target neuron ID
            synapse_data: Binary synapse data
        """
        entry_size = 12
        num_synapses = len(synapse_data) // entry_size
        
        synapses = []
        for i in range(num_synapses):
            offset = i * entry_size
            source_id, weight, delay = struct.unpack_from('<IfI', synapse_data, offset)
            
            synapses.append(Synapse(
                source_neuron_global_id=source_id,
                weight=weight,
                delay_us=delay
            ))
        
        self.synapses[neuron_id] = synapses
    
    def inject_spike(self, spike: Spike):
        """Inject spike into incoming queue."""
        self.incoming_spikes.append(spike)
        self.stats['total_spikes_received'] += 1
    
    def start_execution(self, timestep_us: int = 1000):
        """
        Start SNN execution.
        
        Args:
            timestep_us: Simulation timestep in microseconds
        """
        if self.running:
            return
        
        self.running = True
        self.timestep_us = timestep_us
        self.current_time_us = 0
        
        self.exec_thread = threading.Thread(target=self._execution_loop, daemon=True)
        self.exec_thread.start()
    
    def stop_execution(self):
        """Stop SNN execution."""
        self.running = False
        if self.exec_thread:
            self.exec_thread.join(timeout=1.0)
    
    def _execution_loop(self):
        """Main execution loop."""
        while self.running:
            start = time.time()
            
            # Process incoming spikes
            self._process_spikes()
            
            # Update all neurons
            self._update_neurons()
            
            # Advance time
            self.current_time_us += self.timestep_us
            self.stats['simulation_steps'] += 1
            
            # Sleep to maintain real-time
            elapsed_s = time.time() - start
            timestep_s = self.timestep_us / 1_000_000
            if elapsed_s < timestep_s:
                time.sleep(timestep_s - elapsed_s)
    
    def _process_spikes(self):
        """Process incoming spikes."""
        # Process all spikes in queue
        while self.incoming_spikes:
            spike = self.incoming_spikes.popleft()
            
            # Decode global neuron ID
            source_global_id = (spike.source_backplane << 24) | \
                             (spike.source_node << 16) | \
                             spike.neuron_id
            
            # Find neurons that have this source in their synapse table
            for neuron_id, synapses in self.synapses.items():
                for synapse in synapses:
                    if synapse.source_neuron_global_id == source_global_id:
                        # Apply synaptic input
                        neuron = self.neurons.get(neuron_id)
                        if neuron:
                            neuron.membrane_potential += spike.value * synapse.weight
    
    def _update_neurons(self):
        """Update all neurons (leak and spike generation)."""
        for neuron in self.neurons.values():
            # Check refractory period
            if self.current_time_us - neuron.last_spike_time_us < neuron.refractory_period_us:
                continue
            
            # Apply leak
            neuron.membrane_potential *= neuron.leak_rate
            
            # Check for spike
            if neuron.membrane_potential >= neuron.threshold:
                # Generate spike
                spike = Spike(
                    neuron_id=neuron.neuron_id,
                    source_node=self.node_id,
                    source_backplane=self.backplane_id,
                    timestamp_us=self.current_time_us,
                    value=1.0
                )
                
                self.outgoing_spikes.append(spike)
                self.stats['total_spikes_sent'] += 1
                self.stats['neurons_spiked'] += 1
                
                # Reset membrane potential
                neuron.membrane_potential = 0.0
                neuron.last_spike_time_us = self.current_time_us
    
    def get_outgoing_spikes(self) -> List[Spike]:
        """Get and clear outgoing spike queue."""
        spikes = list(self.outgoing_spikes)
        self.outgoing_spikes.clear()
        return spikes
    
    def get_activity(self) -> Dict:
        """Get current activity statistics."""
        active_neurons = sum(
            1 for n in self.neurons.values() 
            if n.membrane_potential > 0.01
        )
        
        return {
            'neuron_count': len(self.neurons),
            'active_neurons': active_neurons,
            'total_spikes_received': self.stats['total_spikes_received'],
            'total_spikes_sent': self.stats['total_spikes_sent'],
            'neurons_spiked': self.stats['neurons_spiked'],
            'simulation_steps': self.stats['simulation_steps'],
            'current_time_us': self.current_time_us,
            'running': self.running
        }
    
    def get_neuron_state(self, neuron_id: int) -> Optional[Dict]:
        """Get state of specific neuron."""
        neuron = self.neurons.get(neuron_id)
        if not neuron:
            return None
        
        return {
            'neuron_id': neuron.neuron_id,
            'membrane_potential': neuron.membrane_potential,
            'threshold': neuron.threshold,
            'leak_rate': neuron.leak_rate,
            'refractory_period_us': neuron.refractory_period_us,
            'last_spike_time_us': neuron.last_spike_time_us,
            'synapse_count': neuron.synapse_count,
            'flags': neuron.flags
        }


class ClusterSNNCoordinator:
    """Coordinates SNN execution across multiple nodes."""
    
    def __init__(self, cluster):
        """
        Initialize coordinator.
        
        Args:
            cluster: Cluster instance
        """
        self.cluster = cluster
        self.engines: Dict[Tuple[int, int], SNNEngine] = {}  # (backplane_id, node_id) -> engine
        
        # Spike routing thread
        self.routing_active = False
        self.routing_thread: Optional[threading.Thread] = None
    
    def create_engine(self, backplane_id: int, node_id: int) -> SNNEngine:
        """Create SNN engine for node."""
        engine = SNNEngine(node_id, backplane_id)
        self.engines[(backplane_id, node_id)] = engine
        return engine
    
    def get_engine(self, backplane_id: int, node_id: int) -> Optional[SNNEngine]:
        """Get SNN engine for node."""
        return self.engines.get((backplane_id, node_id))
    
    def start_all(self, timestep_us: int = 1000):
        """Start all SNN engines."""
        for engine in self.engines.values():
            engine.start_execution(timestep_us)
        
        # Start spike routing
        self.routing_active = True
        self.routing_thread = threading.Thread(target=self._routing_loop, daemon=True)
        self.routing_thread.start()
    
    def stop_all(self):
        """Stop all SNN engines."""
        self.routing_active = False
        if self.routing_thread:
            self.routing_thread.join(timeout=1.0)
        
        for engine in self.engines.values():
            engine.stop_execution()
    
    def _routing_loop(self):
        """Route spikes between nodes."""
        while self.routing_active:
            # Collect outgoing spikes from all engines
            for (bp_id, node_id), engine in self.engines.items():
                spikes = engine.get_outgoing_spikes()
                
                for spike in spikes:
                    # Route spike to all nodes (broadcast)
                    # In real hardware, this would go over Z1 bus
                    for (target_bp, target_node), target_engine in self.engines.items():
                        if (target_bp, target_node) != (bp_id, node_id):
                            target_engine.inject_spike(spike)
            
            time.sleep(0.001)  # 1ms routing interval
    
    def get_global_activity(self) -> Dict:
        """Get activity across all engines."""
        total_neurons = sum(len(e.neurons) for e in self.engines.values())
        total_spikes_sent = sum(e.stats['total_spikes_sent'] for e in self.engines.values())
        total_spikes_received = sum(e.stats['total_spikes_received'] for e in self.engines.values())
        
        return {
            'total_engines': len(self.engines),
            'total_neurons': total_neurons,
            'total_spikes_sent': total_spikes_sent,
            'total_spikes_received': total_spikes_received,
            'routing_active': self.routing_active
        }
