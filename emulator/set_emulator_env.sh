#!/bin/bash
#
# Set environment variables for Z1 emulator
#
# Usage:
#   source set_emulator_env.sh
#   OR
#   . set_emulator_env.sh
#

# Set controller IP to localhost
export Z1_CONTROLLER_IP=127.0.0.1

# Set controller port to emulator default
export Z1_CONTROLLER_PORT=8000

# Add emulator tools to PATH
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export PATH="${SCRIPT_DIR}/tools:${PATH}"

echo "✓ Z1 emulator environment configured:"
echo "  Z1_CONTROLLER_IP=${Z1_CONTROLLER_IP}"
echo "  Z1_CONTROLLER_PORT=${Z1_CONTROLLER_PORT}"
echo "  Tools added to PATH"
echo ""
echo "You can now use tools without -c flag:"
echo "  nls"
echo "  nsnn deploy examples/xor_snn.json"
echo "  nstat"
