#!/bin/bash
# ChargeControl – package.sh
# Thin wrapper around build.sh for convenience.

set -e
cd "$(dirname "$0")"
bash build.sh "$@"

