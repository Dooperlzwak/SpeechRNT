# Multi-stage build for C++ backend
FROM nvidia/cuda:11.8-devel-ubuntu20.04 as builder

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    curl \
    pkg-config \
    libssl-dev \
    zlib1g-dev \
    libbz2-dev \
    libreadline-dev \
    libsqlite3-dev \
    libncursesw5-dev \
    xz-utils \
    tk-dev \
    libxml2-dev \
    libxmlsec1-dev \
    libffi-dev \
    liblzma-dev \
    && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy CMake files first for better caching
COPY CMakeLists.txt ./
COPY cmake/ ./cmake/

# Copy source code
COPY src/ ./src/
COPY include/ ./include/
COPY third_party/ ./third_party/

# Create build directory and build
RUN mkdir -p build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_CUDA_ARCHITECTURES=75,80,86 \
          .. && \
    make -j$(nproc)

# Runtime stage
FROM nvidia/cuda:11.8-runtime-ubuntu20.04

ENV DEBIAN_FRONTEND=noninteractive

# Install runtime dependencies
RUN apt-get update && apt-get install -y \
    libssl1.1 \
    zlib1g \
    libbz2-1.0 \
    libreadline8 \
    libsqlite3-0 \
    libncursesw6 \
    liblzma5 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy built application from builder stage
COPY --from=builder /app/build/SpeechRNT ./
COPY --from=builder /app/build/lib/ ./lib/

# Copy configuration files
COPY config/ ./config/

# Create data directory for AI models
RUN mkdir -p data

# Expose WebSocket port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Run the application
CMD ["./SpeechRNT", "--port", "8080"]