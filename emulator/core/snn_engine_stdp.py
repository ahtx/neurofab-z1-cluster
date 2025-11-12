"""
SNN Execution Engine with STDP Learning

Implements Leaky Integrate-and-Fire (LIF) neurons with Spike-Timing-Dependent
Plasticity (STDP) for online learning.
"""

import time
import math
import struct
import threading
from typing import Dict, List, Optional, Tuple, Callable
from dataclasses import dataclass, field
from collections import deque


@dataclass
class Neuron:
    """LIF neuron state with STDP support."""
    neuron_id: int
    membrane_potential: float = 0.0
    threshold: float = 1.0
    leak_rate: float = 0.95
    refractory_period_us: int = 1000
    last_spike_time_us: int = 0
    synapse_count: int = 0
    flags: int = 0
    
    # STDP trace for learning
    pre_trace: float = 0.0   # Presynaptic trace
    post_trace: float = 0.0  # Postsynaptic trace


@dataclass
class Synapse:
    """Synapse connection with STDP-modifiable weight."""
    source_neuron_global_id: int  # (backplane_id << 24) | (node_id << 16) | local_id
    weight: float
    delay_us: int = 1000
    
    # STDP parameters
    min_weight: float = 0.0
    max_weight: float = 1.0
    last_update_time_us: int = 0


@dataclass
class Spike:
    """Spike event."""
    neuron_id: int
    source_node: int
    source_backplane: int
    timestamp_us: int
    value: float = 1.0


@dataclass
class STDPConfig:
    """STDP learning configuration."""
    enabled: bool = False
    
    # Learning rates
    learning_rate_plus: float = 0.01   # LTP (Long-Term Potentiation)
    learning_rate_minus: float = 0.01  # LTD (Long-Term Depression)
    
    # Time constants (microseconds)
    tau_plus: float = 20000.0   # 20ms - presynaptic trace decay
    tau_minus: float = 20000.0  # 20ms - postsynaptic trace decay
    
    # Weight bounds
    w_min: float = 0.0
    w_max: float = 1.0
    
    # STDP window
    max_delta_t: int = 100000  # 100ms - maximum time difference for STDP


class SNNEngineSTDP:
    """Simulates SNN execution with STDP learning on a compute node."""
    
    def __init__(self, node_id: int, backplane_id: int, stdp_config: Optional[STDPConfig] = None):
        """
        Initialize SNN engine with STDP support.
        
        Args:
            node_id: Node ID
            backplane_id: Backplane ID
            stdp_config: STDP configuration (None = disabled)
        """
        self.node_id = node_id
        self.backplane_id = backplane_id
        self.stdp_config = stdp_config or STDPConfig(enabled=False)
        
        # Neuron table
        self.neurons: Dict[int, Neuron] = {}
        self.synapses: Dict[int, List[Synapse]] = {}  # neuron_id -> list of synapses
        
        # Spike queues
        self.incoming_spikes: deque = deque()
        self.outgoing_spikes: deque = deque()
        
        # Spike history for STDP (neuron_id -> list of recent spike times)
        self.spike_history: Dict[int, deque] = {}
        
        # Simulation state
        self.running = False
        self.current_time_us = 0
        self.timestep_us = 1000  # 1ms default
        
        # Statistics
        self.stats = {
            'total_spikes_received': 0,
            'total_spikes_sent': 0,
            'neurons_spiked': 0,
            'simulation_steps': 0,
            'stdp_updates': 0,
            'weight_increases': 0,
            'weight_decreases': 0
        }
        
        # Thread for simulation loop
        self.sim_thread: Optional[threading.Thread] = None
        
    def load_neurons_from_table(self, neuron_table: bytes):
        """
        Load neurons from binary neuron table.
        
        Args:
            neuron_table: Binary neuron table data
        """
        self.neurons.clear()
        self.synapses.clear()
        self.spike_history.clear()
        
        entry_size = 256
        num_entries = len(neuron_table) // entry_size
        
        for i in range(num_entries):
            offset = i * entry_size
            entry_data = neuron_table[offset:offset + entry_size]
            
            # Parse neuron entry
            neuron_id, flags, v_mem, threshold, last_spike = struct.unpack_from('<HHffI', entry_data, 0)
            synapse_count, synapse_capacity, _ = struct.unpack_from('<HHI', entry_data, 16)
            leak_rate, refractory_us = struct.unpack_from('<fI', entry_data, 24)
            
            # Create neuron
            neuron = Neuron(
                neuron_id=neuron_id,
                membrane_potential=v_mem,
                threshold=threshold,
                leak_rate=leak_rate,
                refractory_period_us=refractory_us,
                last_spike_time_us=last_spike,
                synapse_count=synapse_count,
                flags=flags
            )
            
            self.neurons[neuron_id] = neuron
            self.spike_history[neuron_id] = deque(maxlen=100)  # Keep last 100 spikes
            
            # Parse synapses
            synapses = []
            for j in range(min(synapse_count, 60)):
                synapse_value = struct.unpack_from('<I', entry_data, 40 + j * 4)[0]
                source_id = (synapse_value >> 8) & 0xFFFFFF
                weight_int = synapse_value & 0xFF
                weight_float = weight_int / 255.0
                
                synapse = Synapse(
                    source_neuron_global_id=source_id,
                    weight=weight_float,
                    delay_us=1000,
                    min_weight=self.stdp_config.w_min,
                    max_weight=self.stdp_config.w_max
                )
                synapses.append(synapse)
            
            self.synapses[neuron_id] = synapses
    
    def start(self):
        """Start SNN execution."""
        if self.running:
            return
        
        self.running = True
        self.current_time_us = 0
        self.sim_thread = threading.Thread(target=self._simulation_loop, daemon=True)
        self.sim_thread.start()
    
    def stop(self):
        """Stop SNN execution."""
        self.running = False
        if self.sim_thread:
            self.sim_thread.join(timeout=2.0)
    
    def inject_spike(self, neuron_id: int, value: float = 1.0):
        """
        Inject external spike into a neuron.
        
        Args:
            neuron_id: Target neuron ID
            value: Spike value (default 1.0)
        """
        spike = Spike(
            neuron_id=neuron_id,
            source_node=self.node_id,
            source_backplane=self.backplane_id,
            timestamp_us=self.current_time_us,
            value=value
        )
        self.incoming_spikes.append(spike)
        self.stats['total_spikes_received'] += 1
        
        print(f"[SNN-{self.node_id}] Injecting spike into neuron {neuron_id}, value={value}")
    
    def _simulation_loop(self):
        """Main simulation loop."""
        while self.running:
            # Update time
            self.current_time_us += self.timestep_us
            self.stats['simulation_steps'] += 1
            
            # Process incoming spikes
            while self.incoming_spikes:
                spike = self.incoming_spikes.popleft()
                self._process_spike(spike)
            
            # Update neuron states (leak)
            for neuron in self.neurons.values():
                if neuron.membrane_potential > 0:
                    neuron.membrane_potential *= neuron.leak_rate
                    
                    # Check for threshold crossing
                    if neuron.membrane_potential >= neuron.threshold:
                        # Check refractory period
                        if self.current_time_us - neuron.last_spike_time_us >= neuron.refractory_period_us:
                            self._fire_neuron(neuron)
            
            # Decay STDP traces
            if self.stdp_config.enabled:
                self._decay_traces()
            
            # Sleep to maintain real-time simulation
            time.sleep(self.timestep_us / 1_000_000.0)
    
    def _process_spike(self, spike: Spike):
        """
        Process incoming spike.
        
        Args:
            spike: Spike event to process
        """
        source_id = spike.neuron_id
        
        print(f"[SNN-{self.node_id}] Processing spike from neuron {source_id} @ node {spike.source_node}, global_id=0x{source_id:08x}")
        
        # Record spike in history for STDP
        if source_id in self.spike_history:
            self.spike_history[source_id].append(self.current_time_us)
        
        # If this is an input neuron on this node, fire it directly
        if source_id in self.neurons:
            neuron = self.neurons[source_id]
            if neuron.synapse_count == 0:  # Input neuron (no incoming synapses)
                print(f"[SNN-{self.node_id}] Neuron {source_id} is input neuron (no synapses), firing directly")
                self._fire_neuron(neuron)
                return
        
        # Apply spike to all neurons that have this source as input
        for neuron_id, synapses in self.synapses.items():
            neuron = self.neurons[neuron_id]
            
            for synapse in synapses:
                if synapse.source_neuron_global_id == source_id:
                    # Apply synaptic weight
                    delta_v = synapse.weight * spike.value
                    old_v = neuron.membrane_potential
                    neuron.membrane_potential += delta_v
                    
                    print(f"[SNN-{self.node_id}]   → Neuron {neuron_id}: V_mem {old_v:.3f} + {delta_v:.3f} = {neuron.membrane_potential:.3f} (threshold={neuron.threshold})")
                    
                    # STDP: Update weight based on spike timing
                    if self.stdp_config.enabled:
                        self._apply_stdp(neuron, synapse, is_pre_spike=True)
    
    def _fire_neuron(self, neuron: Neuron):
        """
        Fire a neuron and generate outgoing spike.
        
        Args:
            neuron: Neuron to fire
        """
        print(f"[SNN-{self.node_id}] ⚡ Neuron {neuron.neuron_id} FIRED! (V_mem={neuron.membrane_potential:.3f}, threshold={neuron.threshold})")
        
        # Record spike
        neuron.last_spike_time_us = self.current_time_us
        self.spike_history[neuron.neuron_id].append(self.current_time_us)
        
        # Reset membrane potential
        neuron.membrane_potential = 0.0
        
        # Update postsynaptic trace for STDP
        if self.stdp_config.enabled:
            neuron.post_trace += 1.0
        
        # Generate outgoing spike
        spike = Spike(
            neuron_id=neuron.neuron_id,
            source_node=self.node_id,
            source_backplane=self.backplane_id,
            timestamp_us=self.current_time_us,
            value=1.0
        )
        self.outgoing_spikes.append(spike)
        
        # Update statistics
        self.stats['total_spikes_sent'] += 1
        self.stats['neurons_spiked'] += 1
        
        print(f"[SNN-{self.node_id}] Spike added to outgoing queue, total sent: {self.stats['total_spikes_sent']}")
        
        # STDP: Update weights for all incoming synapses
        if self.stdp_config.enabled and neuron.neuron_id in self.synapses:
            for synapse in self.synapses[neuron.neuron_id]:
                self._apply_stdp(neuron, synapse, is_pre_spike=False)
    
    def _apply_stdp(self, neuron: Neuron, synapse: Synapse, is_pre_spike: bool):
        """
        Apply STDP weight update.
        
        Args:
            neuron: Postsynaptic neuron
            synapse: Synapse to update
            is_pre_spike: True if presynaptic spike, False if postsynaptic spike
        """
        source_id = synapse.source_neuron_global_id
        
        # Get spike times
        if source_id not in self.spike_history:
            return
        
        pre_spikes = self.spike_history.get(source_id, deque())
        post_spikes = self.spike_history.get(neuron.neuron_id, deque())
        
        if not pre_spikes or not post_spikes:
            return
        
        # Calculate delta_t (time difference)
        if is_pre_spike:
            # Presynaptic spike arrived - check for recent postsynaptic spikes (LTD)
            t_pre = pre_spikes[-1] if pre_spikes else 0
            for t_post in reversed(post_spikes):
                delta_t = t_pre - t_post
                if delta_t > 0 and delta_t <= self.stdp_config.max_delta_t:
                    # Post before pre → LTD (depression)
                    delta_w = -self.stdp_config.learning_rate_minus * math.exp(-delta_t / self.stdp_config.tau_minus)
                    self._update_weight(synapse, delta_w)
                    break
        else:
            # Postsynaptic spike occurred - check for recent presynaptic spikes (LTP)
            t_post = post_spikes[-1] if post_spikes else 0
            for t_pre in reversed(pre_spikes):
                delta_t = t_post - t_pre
                if delta_t > 0 and delta_t <= self.stdp_config.max_delta_t:
                    # Pre before post → LTP (potentiation)
                    delta_w = self.stdp_config.learning_rate_plus * math.exp(-delta_t / self.stdp_config.tau_plus)
                    self._update_weight(synapse, delta_w)
                    break
    
    def _update_weight(self, synapse: Synapse, delta_w: float):
        """
        Update synapse weight with bounds checking.
        
        Args:
            synapse: Synapse to update
            delta_w: Weight change
        """
        old_weight = synapse.weight
        synapse.weight = max(synapse.min_weight, min(synapse.max_weight, synapse.weight + delta_w))
        synapse.last_update_time_us = self.current_time_us
        
        self.stats['stdp_updates'] += 1
        if delta_w > 0:
            self.stats['weight_increases'] += 1
        else:
            self.stats['weight_decreases'] += 1
        
        print(f"[STDP-{self.node_id}] Weight update: {old_weight:.4f} → {synapse.weight:.4f} (Δw={delta_w:+.4f})")
    
    def _decay_traces(self):
        """Decay STDP traces for all neurons."""
        decay_pre = math.exp(-self.timestep_us / self.stdp_config.tau_plus)
        decay_post = math.exp(-self.timestep_us / self.stdp_config.tau_minus)
        
        for neuron in self.neurons.values():
            neuron.pre_trace *= decay_pre
            neuron.post_trace *= decay_post
    
    def get_statistics(self) -> Dict:
        """Get engine statistics."""
        return {
            'node_id': self.node_id,
            'backplane_id': self.backplane_id,
            'total_neurons': len(self.neurons),
            'total_synapses': sum(len(syns) for syns in self.synapses.values()),
            'running': self.running,
            'current_time_us': self.current_time_us,
            **self.stats
        }
    
    def get_neurons_info(self) -> List[Dict]:
        """Get information about all neurons."""
        neurons_info = []
        for neuron in self.neurons.values():
            neurons_info.append({
                'id': neuron.neuron_id,
                'v_mem': neuron.membrane_potential,
                'threshold': neuron.threshold,
                'leak_rate': neuron.leak_rate,
                'synapse_count': len(self.synapses.get(neuron.neuron_id, [])),
                'last_spike_time_us': neuron.last_spike_time_us,
                'pre_trace': neuron.pre_trace,
                'post_trace': neuron.post_trace
            })
        return neurons_info
    
    def get_synapses_info(self) -> Dict[int, List[Dict]]:
        """Get information about all synapses."""
        synapses_info = {}
        for neuron_id, synapses in self.synapses.items():
            synapses_info[neuron_id] = [
                {
                    'source_id': syn.source_neuron_global_id,
                    'weight': syn.weight,
                    'delay_us': syn.delay_us,
                    'last_update_time_us': syn.last_update_time_us
                }
                for syn in synapses
            ]
        return synapses_info


class ClusterSNNCoordinator:
    """Coordinates SNN execution across multiple nodes."""
    
    def __init__(self):
        """Initialize coordinator."""
        self.engines: Dict[Tuple[int, int], SNNEngineSTDP] = {}  # (backplane_id, node_id) -> engine
        self.routing_active = False
        self.stdp_config: Optional[STDPConfig] = None
        
        # Global spike buffer for inter-node communication
        self.global_spike_buffer: deque = deque()
        self.routing_thread: Optional[threading.Thread] = None
    
    def register_engine(self, engine: SNNEngineSTDP):
        """Register an SNN engine."""
        key = (engine.backplane_id, engine.node_id)
        self.engines[key] = engine
    
    def set_stdp_config(self, config: STDPConfig):
        """Set STDP configuration for all engines."""
        self.stdp_config = config
        for engine in self.engines.values():
            engine.stdp_config = config
    
    def start_all(self):
        """Start all engines and routing."""
        for engine in self.engines.values():
            engine.start()
        
        self.routing_active = True
        self.routing_thread = threading.Thread(target=self._routing_loop, daemon=True)
        self.routing_thread.start()
    
    def stop_all(self):
        """Stop all engines and routing."""
        self.routing_active = False
        if self.routing_thread:
            self.routing_thread.join(timeout=2.0)
        
        for engine in self.engines.values():
            engine.stop()
    
    def inject_spike(self, neuron_id: int, backplane_id: int = 0, node_id: int = 0, value: float = 1.0):
        """Inject spike into specific neuron."""
        key = (backplane_id, node_id)
        if key in self.engines:
            self.engines[key].inject_spike(neuron_id, value)
            return 1
        return 0
    
    def _routing_loop(self):
        """Route spikes between nodes."""
        while self.routing_active:
            # Collect spikes from all engines
            for engine in self.engines.values():
                while engine.outgoing_spikes:
                    spike = engine.outgoing_spikes.popleft()
                    self.global_spike_buffer.append(spike)
            
            # Distribute spikes to all engines
            while self.global_spike_buffer:
                spike = self.global_spike_buffer.popleft()
                
                # Send to all engines (they'll filter based on their synapses)
                for engine in self.engines.values():
                    engine.incoming_spikes.append(spike)
            
            time.sleep(0.001)  # 1ms routing interval
    
    def get_statistics(self) -> Dict:
        """Get cluster-wide statistics."""
        total_neurons = sum(len(eng.neurons) for eng in self.engines.values())
        total_spikes_received = sum(eng.stats['total_spikes_received'] for eng in self.engines.values())
        total_spikes_sent = sum(eng.stats['total_spikes_sent'] for eng in self.engines.values())
        total_stdp_updates = sum(eng.stats.get('stdp_updates', 0) for eng in self.engines.values())
        
        return {
            'total_engines': len(self.engines),
            'total_neurons': total_neurons,
            'total_spikes_received': total_spikes_received,
            'total_spikes_sent': total_spikes_sent,
            'total_stdp_updates': total_stdp_updates,
            'routing_active': self.routing_active,
            'stdp_enabled': self.stdp_config.enabled if self.stdp_config else False
        }
