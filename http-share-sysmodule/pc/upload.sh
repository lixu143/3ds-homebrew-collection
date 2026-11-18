#!/usr/bin/env bash
# HTTP Share 3DS - upload a file (curl, works on Linux/macOS/Git Bash)
# Usage: ./upload.sh 192.168.5.119 game.cia [subdir] [port]
set -euo pipefail

IP="${1:?usage: upload.sh <ip> <file> [subdir] [port]}"
FILE="${2:?usage: upload.sh <ip> <file> [subdir] [port]}"
SUBDIR="${3:-}"
PORT="${4:-8080}"

if [ ! -f "$FILE" ]; then echo "File not found: $FILE" >&2; exit 1; fi

echo "Uploading $(basename "$FILE") ($(du -h "$FILE" | cut -f1)) -> http://${IP}:${PORT}/upload"
curl -sS --max-time 600 \
  -F "path=${SUBDIR}" \
  -F "file=@${FILE}" \
  "http://${IP}:${PORT}/upload"
echo
