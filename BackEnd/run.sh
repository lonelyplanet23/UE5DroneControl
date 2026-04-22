#!/bin/bash
cd "$(dirname "$0")"
python -m uvicorn app.main:app --host 0.0.0.0 --port 8080 --reload
