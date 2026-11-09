#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="${SCRIPT_DIR}/.venv"
ESPTOOL="${VENV_DIR}/bin/esptool.py"
MONITOR="${VENV_DIR}/bin/idf-monitor"

# Verify venv exists
if [ ! -f "${ESPTOOL}" ]; then
    echo "⚠️  Project-local .venv not found. Creating it now..."
    /usr/bin/python3 -m venv "${VENV_DIR}"
    "${VENV_DIR}/bin/pip" install --upgrade pip
    "${VENV_DIR}/bin/pip" install -r "${SCRIPT_DIR}/requirements.txt"
    echo "✅ Local virtual environment created successfully."
fi

# Detect serial port
detect_port() {
    local port="$1"
    if [ -n "$port" ] && [ -e "$port" ]; then
        echo "$port"
        return 0
    fi

    # Auto-detect macOS USB modem / serial device
    local found_ports
    found_ports=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* 2>/dev/null || true)
    
    if [ -n "$found_ports" ]; then
        # Pick the first matching port
        echo "$found_ports" | head -n 1
    else
        echo ""
    fi
}

COMMAND="${1:-flash}"
CUSTOM_PORT="$2"
PORT=$(detect_port "$CUSTOM_PORT")

case "$COMMAND" in
    info|chip-id)
        if [ -z "$PORT" ]; then
            echo "❌ No ESP32 device detected on /dev/cu.usbmodem* or /dev/cu.usbserial*."
            exit 1
        fi
        echo "🔌 Checking ESP32 on ${PORT}..."
        "${ESPTOOL}" --port "${PORT}" chip_id
        ;;

    erase)
        if [ -z "$PORT" ]; then
            echo "❌ No ESP32 device detected on /dev/cu.usbmodem* or /dev/cu.usbserial*."
            exit 1
        fi
        echo "⚠️  Erasing entire flash on ${PORT}..."
        "${ESPTOOL}" --port "${PORT}" erase_flash
        echo "✅ Flash erased successfully."
        ;;

    monitor)
        if [ -z "$PORT" ]; then
            echo "❌ No ESP32 device detected on /dev/cu.usbmodem* or /dev/cu.usbserial*."
            exit 1
        fi
        echo "🖥️  Starting serial monitor on ${PORT} (115200 baud)..."
        echo "💡 Press Ctrl+] or Ctrl+C to exit."
        if [ -f "${MONITOR}" ]; then
            "${MONITOR}" --no-reset -p "${PORT}" -b 115200 "${SCRIPT_DIR}/build/christmas_tree_matter.elf" 2>/dev/null || \
            "${MONITOR}" --no-reset -p "${PORT}" -b 115200
        else
            "${VENV_DIR}/bin/pyserial-miniterm" "${PORT}" 115200
        fi
        ;;

    flash)
        if [ -z "$PORT" ]; then
            echo "❌ No ESP32 device detected on /dev/cu.usbmodem* or /dev/cu.usbserial*."
            echo "   Please plug in your ESP32-S3 via USB and try again."
            exit 1
        fi

        BUILD_DIR="${SCRIPT_DIR}/build"
        BOOTLOADER="${BUILD_DIR}/bootloader/bootloader.bin"
        PARTITION_TABLE="${BUILD_DIR}/partition_table/partition-table.bin"
        APP_BIN="${BUILD_DIR}/christmas_tree_matter.bin"

        if [ ! -f "$APP_BIN" ]; then
            echo "❌ Firmware binary not found at ${APP_BIN}."
            echo "   Please build the firmware first via Docker (./build-docker.sh) or idf.py build."
            exit 1
        fi

        echo "🚀 Flashing ESP32-S3 on ${PORT}..."
        FLASH_ARGS=(
            --chip esp32s3
            --port "${PORT}"
            --baud 460800
            --before default_reset
            --after hard_reset
            write_flash
            --flash_mode dio
            --flash_freq 80m
            --flash_size 8MB
            0x0 "${BOOTLOADER}"
            0x8000 "${PARTITION_TABLE}"
            0x20000 "${APP_BIN}"
        )

        if [ -f "${BUILD_DIR}/ota_data_initial.bin" ]; then
            FLASH_ARGS+=(0xf000 "${BUILD_DIR}/ota_data_initial.bin")
        fi

        "${ESPTOOL}" "${FLASH_ARGS[@]}"
        echo "✅ Flashing complete!"
        echo "💡 To monitor serial output, run: ./flash.sh monitor"
        ;;

    setup)
        echo "✅ Environment verified: ${ESPTOOL}"
        ;;

    *)
        echo "Usage: ./flash.sh [command] [port]"
        echo ""
        echo "Commands:"
        echo "  flash      Flash built firmware to ESP32 (default)"
        echo "  monitor    Open serial terminal monitor"
        echo "  info       Read chip info and MAC address"
        echo "  erase      Erase entire flash memory"
        echo "  setup      Ensure .venv and tools are installed"
        exit 1
        ;;
esac
