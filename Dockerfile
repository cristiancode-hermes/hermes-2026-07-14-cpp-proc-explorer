# Stage 1: Build
FROM gcc:14-bookworm AS builder
RUN apt-get update && apt-get install -y cmake pkg-config
WORKDIR /src
COPY CMakeLists.txt CMakePresets.json ./
COPY src/ include/ tests/ ./
RUN cmake --preset debug && cmake --build --preset debug

# Stage 2: Runtime (~25 MB)
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y ca-certificates && rm -rf /var/lib/apt/lists/*
COPY --from=builder /src/build/debug/proc-explorer /usr/local/bin/proc-explorer
ENTRYPOINT ["proc-explorer"]
CMD ["--help"]
