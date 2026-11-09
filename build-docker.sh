#!/usr/bin/env bash
set -e

# Ensure Docker Desktop helper binaries are in PATH
export PATH="/Applications/Docker.app/Contents/Resources/bin:$PATH"

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="espressif/esp-matter:latest"

# Check if Docker daemon is running
if ! docker info >/dev/null 2>&1; then
    echo "❌ Docker daemon is not running."
    echo "💡 Please open Docker Desktop and run this script again."
    exit 1
fi

echo "🚀 Compiling firmware inside official esp-matter container..."
docker run --rm \
    -v "${PROJECT_DIR}":/project \
    "${IMAGE_NAME}" \
    bash -c "cd /project && idf.py build"

echo "✅ Build complete! Firmware binaries are in ${PROJECT_DIR}/build/"
echo "💡 To flash to your ESP32-S3, run: ./flash.sh"
