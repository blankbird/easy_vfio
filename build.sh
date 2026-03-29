#!/bin/bash
# Easy VFIO build script
set -e

BUILD_DIR="build"

case "${1:-all}" in
    all)
        mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
        cmake .. && cmake --build . -- -j$(nproc)
        ;;
    clean)
        rm -rf "$BUILD_DIR"
        ;;
    test)
        mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
        cmake .. && cmake --build . -- -j$(nproc)
        ctest --output-on-failure
        ;;
    rebuild)
        rm -rf "$BUILD_DIR"
        mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
        cmake .. && cmake --build . -- -j$(nproc)
        ;;
    install)
        cd "$BUILD_DIR" && cmake --install .
        ;;
    *)
        echo "Usage: $0 {all|clean|test|rebuild|install}"
        exit 1
        ;;
esac
