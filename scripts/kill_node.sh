#!/usr/bin/env bash
# Simulates a crash of one node. Usage: ./scripts/kill_node.sh node-2
set -euo pipefail
pkill -9 -f "cache_node --id $1 " && echo "Killed $1"
