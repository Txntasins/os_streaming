FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        make \
        gcc-aarch64-linux-gnu \
        binutils-aarch64-linux-gnu \
        libc6-dev-arm64-cross \
        qemu-system-arm \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /os
COPY . /os

# Windows checkouts may turn LF into CRLF, which breaks shell scripts
RUN find scripts -type f -name '*.sh' -exec sed -i 's/\r$//' {} + 2>/dev/null || true

# Build the kernel at image build time (fails early if the code doesn't compile)
RUN make clean && make

# QEMU forwards guest port 80 -> container port 8080
EXPOSE 8080

# Builds disk.img from www/ (if changed) and boots the kernel.
# Quit QEMU with: Ctrl+A, then X
CMD ["make", "run"]
