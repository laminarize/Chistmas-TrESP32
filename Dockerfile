# Dockerfile for building ESP32-S3 Matter Christmas Tree Controller
FROM espressif/idf:v5.2.2

ENV DEBIAN_FRONTEND=noninteractive

# Install host tools needed by connectedhomeip / esp-matter
RUN apt-get update && apt-get install -y --no-install-recommends \
    git \
    pkg-config \
    libssl-dev \
    libdbus-1-dev \
    libglib2.0-dev \
    libavahi-client-dev \
    ninja-build \
    python3-venv \
    python3-dev \
    python3-pip \
    unzip \
    libgirepository1.0-dev \
    libcairo2-dev \
    libreadline-dev \
    && rm -rf /var/lib/apt/lists/*

# Clone and setup esp-matter SDK
ENV ESP_MATTER_PATH=/opt/esp/esp-matter
RUN git clone --depth 1 https://github.com/espressif/esp-matter.git ${ESP_MATTER_PATH} \
    && cd ${ESP_MATTER_PATH} \
    && git submodule update --init --depth 1 \
    && ./install.sh

# Wrap entrypoint to export both ESP-IDF and ESP-Matter environments
RUN printf '#!/usr/bin/env bash\n. /opt/esp/idf/export.sh > /dev/null 2>&1\n. %s/export.sh > /dev/null 2>&1\nexec "$@"\n' "${ESP_MATTER_PATH}" > /opt/esp/entrypoint_matter.sh \
    && chmod +x /opt/esp/entrypoint_matter.sh

WORKDIR /project

ENTRYPOINT ["/opt/esp/entrypoint_matter.sh"]
CMD ["idf.py", "build"]
