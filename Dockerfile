# Reproducible build and test environment for the bootloader.
#
#   docker build -t stm32-bootloader .
#   docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD":/work stm32-bootloader
#
# Append a command to run something else, e.g. `make signed` (needs keys/).
# --user makes files written into the mounted tree belong to you, not root.
#
# Flashing (OpenOCD over the ST-Link) is left to the host, because passing USB
# devices into a container differs between Linux, Windows and macOS.

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi libnewlib-arm-none-eabi \
        gcc libc6-dev make git \
        python3 python3-serial python3-cryptography python3-pytest \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["sh", "-c", "make test && make && python3 -m pytest -q sim/tests"]
