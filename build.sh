#!/usr/bin/env bash
# Render build script — use --no-cache-dir to avoid stale hash mismatches
pip install --upgrade pip
pip install --no-cache-dir -r requirements.txt
