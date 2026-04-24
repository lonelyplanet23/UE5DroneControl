#!/bin/bash
cd "$(dirname "$0")"
mkdir -p data logs
./cpp/drone_backend.exe config.yaml
