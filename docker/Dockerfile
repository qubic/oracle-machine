
ARG BASE_IMAGE="ubuntu:22.04"
ARG PACKAGE_LOCATION=""

# Development Environment
FROM ${BASE_IMAGE} AS develop

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends build-essential git cmake libcurl4-openssl-dev ca-certificates libyaml-cpp-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

CMD ["/bin/bash"]

# Runtime Base
FROM ${BASE_IMAGE} AS runtime-base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        libstdc++6 \
        libcurl4 \
        ca-certificates libyaml-cpp0.7 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN groupadd -r oracle && useradd -r -g oracle oracle

# Create directories
RUN mkdir -p /opt/qubic/oracle-machine \
    && chown -R oracle:oracle /opt/qubic/oracle-machine

WORKDIR /opt/qubic/oracle-machine

ENV PATH="/opt/qubic/oracle-machine/bin:${PATH}"

# Runtime image
FROM ${BASE_IMAGE} AS runtime

ARG PACKAGE_LOCATION

# Copy pre-built binaries from host (NO apt here!)
COPY --chown=oracle:oracle ${PACKAGE_LOCATION}/ /opt/qubic/oracle-machine

USER oracle