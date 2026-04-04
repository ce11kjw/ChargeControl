#!/bin/bash
set -e
echo "Starting ChargeControl Web Server..."
pip3 install flask flask-cors -q 2>/dev/null || true
python3 server.py