#!/bin/bash

BUILD_ALL=1
VERSION=latest

# Parse args
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-all)
      BUILD_ALL="$2"
      shift 2
      ;;
    --version)
      VERSION="$2"
      shift 2
      ;;
    --help|-h)
      echo "Usage: $0 [options]"
      echo
      echo "Options:"
      echo "  --build-all 0|1        Rebuild both runtime and dev image (default: 1)"
      echo "  --version VERSION      Specify build version (default: latest)"
      echo "  --help, -h             Show this help message"
      exit 0
      ;;
    *)
      echo "Unknown option: $1"
      exit 1
      ;;
  esac
done

# If this is enabled both runtime and develop image will be re-built
# otherwise develop image will be built only when it doesn't exist
echo "BUILD_ALL=$BUILD_ALL"
echo "VERSION=$VERSION"

DEV_IMAGE="oracle-machine:dev"
RUNTIME_BASE="oracle-machine:rt"
RUNTIME_IMAGE="oracle-machine:${VERSION}"
DOCKER_SRC_DIR="/opt/qubic/oracle_machine"

if [ "$BUILD_ALL" == 1 ]; then
    # Build the dev image with tool for compilation
    echo "Building the develop Docker image"
    docker build -f docker/Dockerfile --target develop -t ${DEV_IMAGE} .

    # Build runtime base image
    echo "Building the runtime baseDocker image"
    docker build -f docker/Dockerfile --target runtime-base -t ${RUNTIME_BASE} .
fi

# Compile the code if neccessary
package_location=build_docker
build_cmd="rm -rf ${package_location} || true && \
mkdir ${package_location} && \
cd ${package_location} && \
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=./redist && \
make install"
install_cmd="echo "
echo $build_cmd
docker run --rm -v $(pwd):${DOCKER_SRC_DIR}  -u $(id -u) ${DEV_IMAGE} bash -c "cd $DOCKER_SRC_DIR && $build_cmd && $install_cmd"

# Package into a new release image base on runtime time
echo "Packaging..."
docker build -f docker/Dockerfile --build-arg BASE_IMAGE=${RUNTIME_BASE} --build-arg PACKAGE_LOCATION=${package_location}/redist --target runtime -t ${RUNTIME_IMAGE} .