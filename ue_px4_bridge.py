#!/usr/bin/env python3
"""
Compatibility wrapper for UE5 -> PX4 bridge.

This delegates to ue_to_px4_bridge.py so existing workflows can call:
  python ue_px4_bridge.py
"""

from ue_to_px4_bridge import main

if __name__ == "__main__":
    main()
