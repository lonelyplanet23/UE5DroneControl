@echo off
cd /d %~dp0
if not exist data mkdir data
if not exist logs mkdir logs
cpp\drone_backend.exe config.yaml
