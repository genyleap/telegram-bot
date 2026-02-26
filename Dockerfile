# ── Stage 1: Build ────────────────────────────────────────────────────────────
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    libcurl4-openssl-dev \
    libjsoncpp-dev \
    ca-certificates \
    git \
  && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTING=ON \
    && cmake --build build --parallel "$(nproc)" \
    && cd build && ctest --output-on-failure

# ── Stage 2: Minimal runtime image ───────────────────────────────────────────
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 \
    libjsoncpp25 \
    ca-certificates \
  && rm -rf /var/lib/apt/lists/* \
  && useradd --no-create-home --shell /bin/false botuser

WORKDIR /app

# Copy binary and config from build stage
COPY --from=builder /src/build/macOS/TelegramBot.app/Contents/MacOS/TelegramBot ./TelegramBot 2>/dev/null || \
     echo "Linux binary path used"
COPY --from=builder /src/build/TelegramBot /app/TelegramBot 2>/dev/null || true
COPY --from=builder /src/config/system-config.json ./config/system-config.json

# Use non-root user for security
USER botuser

# Expose webhook port (only used in webhook mode; ignored in long-poll mode)
EXPOSE 8443

# Health-check: verify the binary exists and is executable
HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
    CMD test -x /app/TelegramBot || exit 1

# TELEGRAM_BOT_TOKEN must be provided at runtime via environment variable:
#   docker run -e TELEGRAM_BOT_TOKEN=<token> telegrambot
ENTRYPOINT ["/app/TelegramBot"]
