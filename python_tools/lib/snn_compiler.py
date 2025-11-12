#!/usr/bin/env python3
"""
SNN Topology Compiler

Compiles high-level SNN topology definitions into distributed neuron tables
for deployment on Z1 cluster.
"""

import json
import struct
import random
import numpy as np
from typing import Dict, List, Tuple, Any
from dataclasses import dataclass


@dataclass
class NeuronConfig:
    """Configuration for a single neuron."""
    neuron_id: int
    global_id: int
    node_id: int
    flags: int
    threshold: float
    leak_rate: float
    refractory_period_us: int
    synapses: List[Tuple[int, int]]  # List of (source_global_id, weight)


class SNNCompiler:
    """Compiles SNN topology to node-specific neuron tables."""
    
    def __init__(self, topology: Dict[str, Any]):
        """
        Initialize compiler with topology definition.
        
        Args:
            topology: SNN topology dictionary
        """
        self.topology = topology
        self.neurons = []
        self.node_assignments = {}
        self.layer_map = {}
        
    def compile(self) -> Dict[int, bytes]:
        """
        Compile topology to neuron tables.
        
        Returns:
            Dictionary mapping node_id to neuron table binary data
        """
        # Step 1: Assign neurons to nodes
        self._assign_neurons_to_nodes()
        
        # Step 2: Build neuron configurations
        self._build_neuron_configs()
        
        # Step 3: Generate connections
        self._generate_connections()
        
        # Step 4: Compile neuron tables
        neuron_tables = self._compile_neuron_tables()
        
        return neuron_tables
    
    def _assign_neurons_to_nodes(self):
        """Assign neurons to compute nodes."""
        assignment = self.topology.get('node_assignment', {})
        strategy = assignment.get('strategy', 'balanced')
        nodes = assignment.get('nodes', list(range(12)))
        
        total_neurons = self.topology['neuron_count']
        
        if strategy == 'balanced':
            # Evenly distribute neurons across nodes
            neurons_per_node = total_neurons // len(nodes)
            
            neuron_id = 0
            for node_id in nodes:
                node_neurons = []
                for _ in range(neurons_per_node):
                    if neuron_id < total_neurons:
                        node_neurons.append(neuron_id)
                        neuron_id += 1
                self.node_assignments[node_id] = node_neurons
            
            # Assign remaining neurons
            node_idx = 0
            while neuron_id < total_neurons:
                self.node_assignments[nodes[node_idx]].append(neuron_id)
                neuron_id += 1
                node_idx = (node_idx + 1) % len(nodes)
        
        elif strategy == 'layer_based':
            # Assign entire layers to nodes
            layers = self.topology['layers']
            node_idx = 0
            
            for layer in layers:
                start_id = layer['neuron_ids'][0]
                end_id = layer['neuron_ids'][1]
                
                node_id = nodes[node_idx % len(nodes)]
                if node_id not in self.node_assignments:
                    self.node_assignments[node_id] = []
                
                for neuron_id in range(start_id, end_id + 1):
                    self.node_assignments[node_id].append(neuron_id)
                
                node_idx += 1
    
    def _build_neuron_configs(self):
        """Build neuron configurations from layers."""
        layers = self.topology['layers']
        
        for layer in layers:
            layer_id = layer['layer_id']
            layer_type = layer['layer_type']
            start_id = layer['neuron_ids'][0]
            end_id = layer['neuron_ids'][1]
            
            # Determine neuron flags
            flags = 0x0001  # ACTIVE
            if layer_type == 'input':
                flags |= 0x0004  # INPUT
            elif layer_type == 'output':
                flags |= 0x0008  # OUTPUT
            
            # Get layer parameters
            threshold = layer.get('threshold', 1.0)
            leak_rate = layer.get('leak_rate', 0.95)
            refractory_period_us = layer.get('refractory_period_us', 1000)
            
            # Create neuron configs
            for global_id in range(start_id, end_id + 1):
                # Find which node this neuron is assigned to
                node_id = self._find_node_for_neuron(global_id)
                local_id = self.node_assignments[node_id].index(global_id)
                
                neuron = NeuronConfig(
                    neuron_id=local_id,
                    global_id=global_id,
                    node_id=node_id,
                    flags=flags,
                    threshold=threshold,
                    leak_rate=leak_rate,
                    refractory_period_us=refractory_period_us,
                    synapses=[]
                )
                
                self.neurons.append(neuron)
                self.layer_map[global_id] = layer_id
    
    def _find_node_for_neuron(self, global_id: int) -> int:
        """Find which node a neuron is assigned to."""
        for node_id, neuron_list in self.node_assignments.items():
            if global_id in neuron_list:
                return node_id
        raise ValueError(f"Neuron {global_id} not assigned to any node")
    
    def _generate_connections(self):
        """Generate synaptic connections based on topology."""
        connections = self.topology.get('connections', [])
        layers = self.topology['layers']
        
        for conn in connections:
            source_layer_id = conn['source_layer']
            target_layer_id = conn['target_layer']
            conn_type = conn['connection_type']
            
            # Find source and target layers
            source_layer = next(l for l in layers if l['layer_id'] == source_layer_id)
            target_layer = next(l for l in layers if l['layer_id'] == target_layer_id)
            
            source_start = source_layer['neuron_ids'][0]
            source_end = source_layer['neuron_ids'][1]
            target_start = target_layer['neuron_ids'][0]
            target_end = target_layer['neuron_ids'][1]
            
            if conn_type == 'fully_connected':
                self._generate_fully_connected(
                    source_start, source_end,
                    target_start, target_end,
                    conn
                )
            elif conn_type == 'sparse_random':
                self._generate_sparse_random(
                    source_start, source_end,
                    target_start, target_end,
                    conn
                )
    
    def _generate_fully_connected(self, source_start: int, source_end: int,
                                  target_start: int, target_end: int,
                                  conn_config: Dict[str, Any]):
        """Generate fully connected layer."""
        weight_init = conn_config.get('weight_init', 'random_normal')
        weight_mean = conn_config.get('weight_mean', 0.5)
        weight_stddev = conn_config.get('weight_stddev', 0.1)
        
        for target_id in range(target_start, target_end + 1):
            target_neuron = next(n for n in self.neurons if n.global_id == target_id)
            
            for source_id in range(source_start, source_end + 1):
                # Generate weight
                if weight_init == 'random_normal':
                    weight_float = random.gauss(weight_mean, weight_stddev)
                    weight_float = max(0.0, min(1.0, weight_float))
                elif weight_init == 'random_uniform':
                    weight_min = conn_config.get('weight_min', 0.0)
                    weight_max = conn_config.get('weight_max', 1.0)
                    weight_float = random.uniform(weight_min, weight_max)
                elif weight_init == 'constant':
                    weight_float = conn_config.get('weight_value', 0.5)
                else:
                    weight_float = 0.5
                
                # Convert to 8-bit integer
                weight = int(weight_float * 255)
                
                # Add synapse (limit to max synapses)
                if len(target_neuron.synapses) < 60:
                    target_neuron.synapses.append((source_id, weight))
    
    def _generate_sparse_random(self, source_start: int, source_end: int,
                                target_start: int, target_end: int,
                                conn_config: Dict[str, Any]):
        """Generate sparse random connections."""
        connection_prob = conn_config.get('connection_probability', 0.1)
        weight_mean = conn_config.get('weight_mean', 0.5)
        weight_stddev = conn_config.get('weight_stddev', 0.1)
        
        for target_id in range(target_start, target_end + 1):
            target_neuron = next(n for n in self.neurons if n.global_id == target_id)
            
            for source_id in range(source_start, source_end + 1):
                if random.random() < connection_prob:
                    # Generate weight
                    weight_float = random.gauss(weight_mean, weight_stddev)
                    weight_float = max(0.0, min(1.0, weight_float))
                    weight = int(weight_float * 255)
                    
                    # Add synapse (limit to max synapses)
                    if len(target_neuron.synapses) < 60:
                        target_neuron.synapses.append((source_id, weight))
    
    def _compile_neuron_tables(self) -> Dict[int, bytes]:
        """Compile neuron tables for each node."""
        neuron_tables = {}
        
        for node_id, neuron_ids in self.node_assignments.items():
            table_data = bytearray()
            
            # Get neurons for this node
            node_neurons = [n for n in self.neurons if n.node_id == node_id]
            node_neurons.sort(key=lambda n: n.neuron_id)
            
            for neuron in node_neurons:
                # Pack neuron entry (256 bytes)
                entry = self._pack_neuron_entry(neuron)
                table_data.extend(entry)
            
            neuron_tables[node_id] = bytes(table_data)
        
        return neuron_tables
    
    def _pack_neuron_entry(self, neuron: NeuronConfig) -> bytes:
        """Pack neuron entry into 256-byte binary format."""
        entry = bytearray(256)
        
        # Neuron state (16 bytes)
        struct.pack_into('<HHffI', entry, 0,
                        neuron.neuron_id,
                        neuron.flags,
                        0.0,  # Initial membrane potential
                        neuron.threshold,
                        0)    # Last spike time
        
        # Synapse metadata (8 bytes)
        struct.pack_into('<HHI', entry, 16,
                        len(neuron.synapses),  # synapse_count
                        60,                    # synapse_capacity
                        0)                     # reserved
        
        # Neuron parameters (8 bytes)
        struct.pack_into('<fI', entry, 24,
                        neuron.leak_rate,
                        neuron.refractory_period_us)
        
        # Reserved (8 bytes) - already zero
        
        # Synapses (240 bytes, 60 × 4 bytes)
        for i, (source_id, weight) in enumerate(neuron.synapses[:60]):
            # Pack synapse: [source_id:24][weight:8]
            synapse_value = ((source_id & 0xFFFFFF) << 8) | (weight & 0xFF)
            struct.pack_into('<I', entry, 40 + i * 4, synapse_value)
        
        return bytes(entry)
    
    def get_deployment_info(self) -> Dict[str, Any]:
        """Get deployment information."""
        return {
            'network_name': self.topology.get('network_name', 'unnamed'),
            'neuron_count': self.topology['neuron_count'],
            'nodes_used': len(self.node_assignments),
            'neurons_per_node': {
                node_id: len(neurons) 
                for node_id, neurons in self.node_assignments.items()
            },
            'total_synapses': sum(len(n.synapses) for n in self.neurons)
        }


def compile_snn_topology(topology_file: str) -> Tuple[Dict[int, bytes], Dict[str, Any]]:
    """
    Compile SNN topology file to neuron tables.
    
    Args:
        topology_file: Path to topology JSON file
        
    Returns:
        Tuple of (neuron_tables, deployment_info)
    """
    with open(topology_file, 'r') as f:
        topology = json.load(f)
    
    compiler = SNNCompiler(topology)
    neuron_tables = compiler.compile()
    deployment_info = compiler.get_deployment_info()
    
    return neuron_tables, deployment_info


if __name__ == '__main__':
    import sys
    
    if len(sys.argv) < 2:
        print("Usage: snn_compiler.py <topology.json>")
        sys.exit(1)
    
    topology_file = sys.argv[1]
    neuron_tables, info = compile_snn_topology(topology_file)
    
    print(f"Compiled SNN: {info['network_name']}")
    print(f"  Neurons: {info['neuron_count']}")
    print(f"  Nodes: {info['nodes_used']}")
    print(f"  Total synapses: {info['total_synapses']}")
    print(f"\nNeurons per node:")
    for node_id, count in info['neurons_per_node'].items():
        print(f"  Node {node_id}: {count} neurons ({len(neuron_tables[node_id])} bytes)")
