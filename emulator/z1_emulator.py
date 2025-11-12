#!/usr/bin/env python3
"""
Z1 Cluster Emulator

Main launcher for the Z1 neuromorphic cluster emulator.
"""

import os
import sys
import json
import argparse
from pathlib import Path

# Add core to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), 'core'))

from cluster import Cluster
from api_server import Z1APIServer


def load_config(config_file: str) -> dict:
    """Load configuration from JSON file."""
    if not os.path.exists(config_file):
        print(f"Config file not found: {config_file}")
        print("Using default configuration")
        return None
    
    with open(config_file, 'r') as f:
        return json.load(f)


def create_default_config(output_file: str):
    """Create default configuration file."""
    config = {
        "backplanes": [
            {
                "name": "backplane-0",
                "controller_ip": "127.0.0.1",
                "controller_port": 8000,
                "node_count": 16,
                "description": "Default emulated backplane"
            }
        ],
        "simulation": {
            "mode": "real-time",
            "timestep_us": 1000,
            "bus_latency_us": 2.0
        },
        "memory": {
            "flash_size_mb": 2,
            "psram_size_mb": 8
        },
        "server": {
            "host": "127.0.0.1",
            "port": 8000
        }
    }
    
    with open(output_file, 'w') as f:
        json.dump(config, f, indent=2)
    
    print(f"Created default configuration: {output_file}")


def main():
    """Main entry point."""
    parser = argparse.ArgumentParser(
        description='Z1 Neuromorphic Cluster Emulator',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # Run with default configuration
  python3 z1_emulator.py
  
  # Run with custom configuration
  python3 z1_emulator.py -c my_cluster.json
  
  # Create default configuration file
  python3 z1_emulator.py --create-config emulator_config.json
  
  # Run on custom host/port
  python3 z1_emulator.py --host 0.0.0.0 --port 8080
  
  # Run with multiple backplanes
  python3 z1_emulator.py -c multi_backplane.json
        '''
    )
    
    parser.add_argument('-c', '--config', 
                       help='Configuration file (JSON)',
                       default=None)
    parser.add_argument('--create-config',
                       help='Create default configuration file and exit',
                       metavar='FILE')
    parser.add_argument('--host',
                       help='Server host (default: 127.0.0.1)',
                       default=None)
    parser.add_argument('--port', type=int,
                       help='Server port (default: 8000)',
                       default=None)
    parser.add_argument('--debug', action='store_true',
                       help='Enable debug mode')
    parser.add_argument('-v', '--verbose', action='store_true',
                       help='Verbose output')
    
    args = parser.parse_args()
    
    # Handle --create-config
    if args.create_config:
        create_default_config(args.create_config)
        return 0
    
    # Load configuration
    config = load_config(args.config) if args.config else None
    
    # Override host/port if specified
    if config and (args.host or args.port):
        if 'server' not in config:
            config['server'] = {}
        if args.host:
            config['server']['host'] = args.host
        if args.port:
            config['server']['port'] = args.port
    
    # Create cluster
    print("Initializing Z1 Cluster Emulator...")
    cluster = Cluster(config)
    
    # Start simulation
    print("Starting simulation thread...")
    cluster.start_simulation()
    
    # Get server config
    server_config = config.get('server', {}) if config else {}
    host = server_config.get('host', '127.0.0.1')
    port = server_config.get('port', 8000)
    
    # Create API server
    print(f"Starting HTTP API server on {host}:{port}...")
    server = Z1APIServer(cluster, host=host, port=port)
    
    # Print cluster info
    info = cluster.get_cluster_info()
    print("\n" + "="*60)
    print("Z1 Cluster Emulator Running")
    print("="*60)
    print(f"Backplanes: {info['total_backplanes']}")
    print(f"Nodes:      {info['total_nodes']}")
    print(f"API Server: http://{host}:{port}")
    print(f"Status:     http://{host}:{port}/api/emulator/status")
    print("="*60)
    print("\nPress Ctrl+C to stop\n")
    
    try:
        # Run server (blocking)
        server.run(debug=args.debug)
    except KeyboardInterrupt:
        print("\n\nShutting down...")
        cluster.stop_simulation()
        print("Emulator stopped.")
        return 0


if __name__ == '__main__':
    sys.exit(main())
