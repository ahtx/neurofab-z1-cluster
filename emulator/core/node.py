"""
Z1 Compute Node Emulator

Simulates an RP2350B-based compute node with 2MB Flash and 8MB PSRAM.
"""

import time
import struct
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, field
from enum import Enum


class NodeStatus(Enum):
    """Node status states."""
    INACTIVE = 0
    ACTIVE = 1
    ERROR = 2
    BOOTLOADER = 3


@dataclass
class LEDState:
    """RGB LED state."""
    r: int = 0
    g: int = 0
    b: int = 0


@dataclass
class FirmwareHeader:
    """Firmware header structure (256 bytes)."""
    magic: int = 0x4E465A31  # 'NF' 'Z1'
    version: int = 1
    firmware_size: int = 0
    crc32: int = 0
    name: str = ""
    description: str = ""
    build_timestamp: int = 0
    
    @classmethod
    def from_bytes(cls, data: bytes) -> 'FirmwareHeader':
        """Parse firmware header from bytes."""
        if len(data) < 256:
            raise ValueError("Firmware header must be 256 bytes")
        
        magic, version, firmware_size, crc32 = struct.unpack_from('<4I', data, 0)
        name = data[16:48].decode('utf-8', errors='ignore').rstrip('\x00')
        description = data[48:176].decode('utf-8', errors='ignore').rstrip('\x00')
        build_timestamp = struct.unpack_from('<Q', data, 176)[0]
        
        return cls(
            magic=magic,
            version=version,
            firmware_size=firmware_size,
            crc32=crc32,
            name=name,
            description=description,
            build_timestamp=build_timestamp
        )
    
    def to_bytes(self) -> bytes:
        """Convert firmware header to bytes."""
        header = bytearray(256)
        struct.pack_into('<4I', header, 0, self.magic, self.version, 
                        self.firmware_size, self.crc32)
        name_bytes = self.name.encode('utf-8')[:32]
        header[16:16+len(name_bytes)] = name_bytes
        desc_bytes = self.description.encode('utf-8')[:128]
        header[48:48+len(desc_bytes)] = desc_bytes
        struct.pack_into('<Q', header, 176, self.build_timestamp)
        return bytes(header)


class Memory:
    """Simulates Flash and PSRAM memory."""
    
    def __init__(self, flash_size: int = 2*1024*1024, psram_size: int = 8*1024*1024):
        """
        Initialize memory.
        
        Args:
            flash_size: Flash size in bytes (default 2MB)
            psram_size: PSRAM size in bytes (default 8MB)
        """
        self.flash = bytearray(flash_size)
        self.psram = bytearray(psram_size)
        
        # Memory map
        self.FLASH_BASE = 0x10000000
        self.PSRAM_BASE = 0x20000000
        
        # Flash regions
        self.BOOTLOADER_BASE = 0x10000000
        self.BOOTLOADER_SIZE = 16 * 1024  # 16KB
        self.APP_BASE = 0x10004000
        self.APP_SIZE = 112 * 1024  # 112KB
        self.FIRMWARE_BUFFER_BASE = 0x10020000
        self.FIRMWARE_BUFFER_SIZE = 128 * 1024  # 128KB
        
        # PSRAM regions
        self.BOOTLOADER_RAM_BASE = 0x20000000
        self.BOOTLOADER_RAM_SIZE = 1 * 1024 * 1024  # 1MB
        self.APP_DATA_BASE = 0x20100000
        self.APP_DATA_SIZE = 7 * 1024 * 1024  # 7MB
    
    def read(self, address: int, length: int) -> bytes:
        """
        Read from memory.
        
        Args:
            address: Memory address
            length: Number of bytes to read
            
        Returns:
            Bytes read from memory
        """
        if address >= self.PSRAM_BASE:
            # PSRAM access
            offset = address - self.PSRAM_BASE
            if offset + length > len(self.psram):
                raise ValueError(f"PSRAM read out of bounds: 0x{address:08X}")
            return bytes(self.psram[offset:offset+length])
        elif address >= self.FLASH_BASE:
            # Flash access
            offset = address - self.FLASH_BASE
            if offset + length > len(self.flash):
                raise ValueError(f"Flash read out of bounds: 0x{address:08X}")
            return bytes(self.flash[offset:offset+length])
        else:
            raise ValueError(f"Invalid memory address: 0x{address:08X}")
    
    def write(self, address: int, data: bytes) -> int:
        """
        Write to memory.
        
        Args:
            address: Memory address
            data: Data to write
            
        Returns:
            Number of bytes written
        """
        length = len(data)
        
        if address >= self.PSRAM_BASE:
            # PSRAM access (always writable)
            offset = address - self.PSRAM_BASE
            if offset + length > len(self.psram):
                raise ValueError(f"PSRAM write out of bounds: 0x{address:08X}")
            self.psram[offset:offset+length] = data
            return length
        elif address >= self.FLASH_BASE:
            # Flash access
            offset = address - self.FLASH_BASE
            if offset + length > len(self.flash):
                raise ValueError(f"Flash write out of bounds: 0x{address:08X}")
            
            # Check if writing to protected bootloader region
            if address < self.APP_BASE:
                raise ValueError("Cannot write to protected bootloader region")
            
            self.flash[offset:offset+length] = data
            return length
        else:
            raise ValueError(f"Invalid memory address: 0x{address:08X}")
    
    def get_info(self) -> Dict:
        """Get memory information."""
        return {
            'flash_size': len(self.flash),
            'psram_size': len(self.psram),
            'flash_base': self.FLASH_BASE,
            'psram_base': self.PSRAM_BASE,
            'bootloader_base': self.BOOTLOADER_BASE,
            'app_base': self.APP_BASE,
            'app_data_base': self.APP_DATA_BASE
        }


class ComputeNode:
    """Simulates a Z1 compute node."""
    
    def __init__(self, node_id: int, backplane_id: int = 0):
        """
        Initialize compute node.
        
        Args:
            node_id: Node ID (0-15)
            backplane_id: Backplane ID
        """
        self.node_id = node_id
        self.backplane_id = backplane_id
        self.status = NodeStatus.ACTIVE
        self.memory = Memory()
        self.led = LEDState(r=0, g=255, b=0)  # Green = active
        
        # Timing
        self.start_time = time.time()
        self.last_activity = time.time()
        
        # Statistics
        self.stats = {
            'memory_reads': 0,
            'memory_writes': 0,
            'bus_messages_received': 0,
            'bus_messages_sent': 0,
            'spikes_received': 0,
            'spikes_sent': 0
        }
        
        # Firmware info
        self.firmware_header: Optional[FirmwareHeader] = None
        self.boot_mode = 'application'  # 'application' or 'bootloader'
        
        # SNN state
        self.snn_running = False
        self.neuron_count = 0
        self.spike_count = 0
        
        # Message queue (simulates bus receive buffer)
        self.message_queue: List[Tuple[int, bytes]] = []
    
    def get_uptime_ms(self) -> int:
        """Get uptime in milliseconds."""
        return int((time.time() - self.start_time) * 1000)
    
    def get_free_memory(self) -> int:
        """Get free PSRAM in bytes (simplified)."""
        # In real hardware, this would track allocations
        # For emulator, assume neuron tables use memory
        used = self.neuron_count * 256  # 256 bytes per neuron
        return self.memory.APP_DATA_SIZE - used
    
    def read_memory(self, address: int, length: int) -> bytes:
        """Read from node memory."""
        self.stats['memory_reads'] += 1
        self.last_activity = time.time()
        return self.memory.read(address, length)
    
    def write_memory(self, address: int, data: bytes) -> int:
        """Write to node memory."""
        self.stats['memory_writes'] += 1
        self.last_activity = time.time()
        return self.memory.write(address, data)
    
    def load_firmware(self, firmware_data: bytes) -> bool:
        """
        Load firmware into update buffer and install.
        
        Args:
            firmware_data: Complete firmware image with header
            
        Returns:
            True if successful
        """
        try:
            # Parse header
            header = FirmwareHeader.from_bytes(firmware_data[:256])
            
            # Verify magic
            if header.magic != 0x4E465A31:
                raise ValueError(f"Invalid firmware magic: 0x{header.magic:08X}")
            
            # Write to firmware buffer
            self.memory.write(self.memory.FIRMWARE_BUFFER_BASE, firmware_data)
            
            # Verify CRC32 (simplified - just check size)
            if len(firmware_data) < 256 + header.firmware_size:
                raise ValueError("Firmware size mismatch")
            
            # Install to application slot
            self.memory.write(self.memory.APP_BASE, firmware_data)
            
            # Update firmware info
            self.firmware_header = header
            
            return True
        except Exception as e:
            print(f"Firmware load failed: {e}")
            return False
    
    def reset(self):
        """Reset node."""
        self.status = NodeStatus.ACTIVE
        self.snn_running = False
        self.spike_count = 0
        self.message_queue.clear()
        self.led = LEDState(r=0, g=255, b=0)
    
    def set_led(self, r: int, g: int, b: int):
        """Set LED color."""
        self.led.r = max(0, min(255, r))
        self.led.g = max(0, min(255, g))
        self.led.b = max(0, min(255, b))
    
    def receive_message(self, command: int, data: bytes):
        """Receive message from bus."""
        self.message_queue.append((command, data))
        self.stats['bus_messages_received'] += 1
        self.last_activity = time.time()
    
    def get_info(self) -> Dict:
        """Get node information."""
        return {
            'node_id': self.node_id,
            'backplane_id': self.backplane_id,
            'status': self.status.name.lower(),
            'uptime_ms': self.get_uptime_ms(),
            'free_memory': self.get_free_memory(),
            'led_state': {
                'r': self.led.r,
                'g': self.led.g,
                'b': self.led.b
            },
            'snn_running': self.snn_running,
            'neuron_count': self.neuron_count,
            'spike_count': self.spike_count,
            'firmware': {
                'name': self.firmware_header.name if self.firmware_header else 'None',
                'version': self.firmware_header.version if self.firmware_header else 0
            },
            'stats': self.stats.copy()
        }
