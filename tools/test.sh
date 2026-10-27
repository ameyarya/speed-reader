#!/bin/sh
# Builds and runs the host tests for the reading engine.
set -e
cd "$(dirname "$0")/.."
mkdir -p .build
c++ -std=c++17 -Wall -Wextra -O1 -g -fsanitize=undefined \
    -Ilib/rsvp/src lib/rsvp/src/*.cpp test/host_test.cpp -o .build/host_test
./.build/host_test
