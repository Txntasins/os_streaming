FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
    gcc-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    qemu-system-arm \
    make \
    findutils \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /os

COPY . /os

CMD ["make", "run"]