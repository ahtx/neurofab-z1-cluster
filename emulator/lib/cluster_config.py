#!/usr/bin/env python3
"""
Cluster Configuration Management

Manages multi-backplane Z1 cluster configurations with multiple controllers.
"""

import os
import json
from typing import List, Dict, Optional
from dataclasses import dataclass


@dataclass
class BackplaneConfig:
    """Configuration for a single backplane."""
    name: str
    controller_ip: str
    controller_port: int = 80
    node_count: int = 16
    description: str = ""
    
    def __str__(self):
        return f"{self.name} ({self.controller_ip})"


class ClusterConfig:
    """Multi-backplane cluster configuration."""
    
    def __init__(self, config_file: Optional[str] = None):
        """
        Initialize cluster configuration.
        
        Args:
            config_file: Path to configuration file (JSON or YAML)
        """
        self.backplanes: List[BackplaneConfig] = []
        self.config_file = config_file
        
        if config_file and os.path.exists(config_file):
            self.load(config_file)
        else:
            # Try default locations
            self._load_default_config()
    
    def _load_default_config(self):
        """Load configuration from default locations."""
        default_paths = [
            os.path.expanduser('~/.neurofab/cluster.json'),
            '/etc/neurofab/cluster.json',
            './cluster.json'
        ]
        
        for path in default_paths:
            if os.path.exists(path):
                self.load(path)
                return
    
    def load(self, config_file: str):
        """
        Load configuration from file.
        
        Args:
            config_file: Path to configuration file
        """
        with open(config_file, 'r') as f:
            config_data = json.load(f)
        
        self.backplanes = []
        for bp_data in config_data.get('backplanes', []):
            backplane = BackplaneConfig(
                name=bp_data['name'],
                controller_ip=bp_data['controller_ip'],
                controller_port=bp_data.get('controller_port', 80),
                node_count=bp_data.get('node_count', 16),
                description=bp_data.get('description', '')
            )
            self.backplanes.append(backplane)
        
        self.config_file = config_file
    
    def save(self, config_file: Optional[str] = None):
        """
        Save configuration to file.
        
        Args:
            config_file: Path to save configuration (uses loaded path if None)
        """
        if config_file is None:
            config_file = self.config_file
        
        if config_file is None:
            raise ValueError("No configuration file specified")
        
        config_data = {
            'backplanes': [
                {
                    'name': bp.name,
                    'controller_ip': bp.controller_ip,
                    'controller_port': bp.controller_port,
                    'node_count': bp.node_count,
                    'description': bp.description
                }
                for bp in self.backplanes
            ]
        }
        
        # Create directory if it doesn't exist
        os.makedirs(os.path.dirname(config_file), exist_ok=True)
        
        with open(config_file, 'w') as f:
            json.dump(config_data, f, indent=2)
    
    def add_backplane(self, backplane: BackplaneConfig):
        """Add a backplane to the configuration."""
        self.backplanes.append(backplane)
    
    def remove_backplane(self, name: str):
        """Remove a backplane by name."""
        self.backplanes = [bp for bp in self.backplanes if bp.name != name]
    
    def get_backplane(self, name: str) -> Optional[BackplaneConfig]:
        """Get a backplane by name."""
        for bp in self.backplanes:
            if bp.name == name:
                return bp
        return None
    
    def get_backplane_by_ip(self, ip: str) -> Optional[BackplaneConfig]:
        """Get a backplane by controller IP."""
        for bp in self.backplanes:
            if bp.controller_ip == ip:
                return bp
        return None
    
    def get_all_controllers(self) -> List[str]:
        """Get list of all controller IPs."""
        return [bp.controller_ip for bp in self.backplanes]
    
    def get_total_nodes(self) -> int:
        """Get total number of nodes across all backplanes."""
        return sum(bp.node_count for bp in self.backplanes)
    
    def __len__(self):
        """Return number of backplanes."""
        return len(self.backplanes)
    
    def __iter__(self):
        """Iterate over backplanes."""
        return iter(self.backplanes)


def create_default_config(output_file: str):
    """
    Create a default configuration file.
    
    Args:
        output_file: Path to output file
    """
    config = ClusterConfig()
    
    # Add example backplanes
    config.add_backplane(BackplaneConfig(
        name="backplane-0",
        controller_ip="192.168.1.222",
        controller_port=80,
        node_count=16,
        description="Primary backplane"
    ))
    
    config.add_backplane(BackplaneConfig(
        name="backplane-1",
        controller_ip="192.168.1.223",
        controller_port=80,
        node_count=16,
        description="Secondary backplane"
    ))
    
    config.save(output_file)
    print(f"Created default configuration: {output_file}")


def get_cluster_config(config_file: Optional[str] = None) -> ClusterConfig:
    """
    Get cluster configuration (singleton pattern).
    
    Args:
        config_file: Optional path to configuration file
        
    Returns:
        ClusterConfig instance
    """
    return ClusterConfig(config_file)


if __name__ == '__main__':
    import sys
    
    if len(sys.argv) > 1:
        create_default_config(sys.argv[1])
    else:
        create_default_config('cluster.json')
