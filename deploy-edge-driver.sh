#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
EDGE_DIR="${SCRIPT_DIR}/edge-driver"
DEFAULT_CHANNEL_ID="${ST_CHANNEL_ID:-2bc9214d-be6d-404b-8b80-36ca37ea8bae}"
DEFAULT_HUB_ID="${ST_HUB_ID:-db484ce1-93b2-40a4-8720-e4f3063ef3ed}"

echo "🌲 Deploying Christmas TrESP32 Local SmartThings Edge Driver..."

if ! command -v smartthings >/dev/null 2>&1; then
    echo "📦 Installing official Samsung @smartthings/cli via Homebrew..."
    brew tap smartthingscommunity/smartthings
    brew trust smartthingscommunity/smartthings || true
    brew install smartthings
fi

echo "🏠 Ensuring Hub (${DEFAULT_HUB_ID}) is enrolled in channel (${DEFAULT_CHANNEL_ID})..."
smartthings edge:channels:enroll "${DEFAULT_HUB_ID}" -C "${DEFAULT_CHANNEL_ID}" || true

echo "🚀 Packaging, uploading, assigning, and installing edge-driver/ to your SmartThings Hub..."
smartthings edge:drivers:package "${EDGE_DIR}" --channel "${DEFAULT_CHANNEL_ID}" --hub "${DEFAULT_HUB_ID}"

echo ""
echo "✅ Edge Driver installed on your SmartThings Hub!"
