#!/usr/bin/env bash

# Need to manually set the time to prevent failing
export SOURCE_DATE_EPOCH=315532802

# Compile the benchmark
(
    cd workloads/simple-test
    mvn package
)

bash configure
make images
