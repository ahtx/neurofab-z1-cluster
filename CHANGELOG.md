# Changelog

## [1.0.0] - 2025-11-12

### Added
- Complete SNN execution engine for hardware and emulator
- STDP learning implementation in emulator
- Multi-backplane support for 200+ nodes
- Software emulator for hardware-free development
- Remote firmware flashing with `nflash` utility
- Comprehensive build system with CMake
- Extensive documentation (250+ pages)

### Fixed
- Neuron ID mapping bug in SNN compiler
- Emulator import errors
- API field compatibility between tools and emulator
- Hardcoded IPs and ports in all tools
- Missing firmware implementations (SNN engine, controller endpoints)

### Changed
- Consolidated all documentation into a clean, streamlined structure
- Improved error handling and verbose output in tools
- Refactored `cluster_config.py` for smart defaults and environment variable support
- Renamed `ncp` for firmware to `nflash` to avoid confusion
