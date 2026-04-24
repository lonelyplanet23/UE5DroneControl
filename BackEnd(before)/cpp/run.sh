#!/bin/bash
cd "$(dirname "$0")"
mkdir -p ../data ../logs
./drone_backend.exe ../config.yaml
