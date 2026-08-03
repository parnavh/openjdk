#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 3 ]; then
    echo "Usage: $0 <iterations> <profile_reuse_file> <profile_measure_file>"
    exit 1
fi

ITERATIONS="$1"
PROFILE_REUSE_FILE="$2"
PROFILE_MEASURE_FILE="$3"

JAVA="build/linux-x86_64-server-release/jdk/bin/java"
JAR="workloads/simple-test/target/simple-test-1.0-SNAPSHOT.jar"

for ((i=1; i<=ITERATIONS; i++)); do
    echo "========================================"
    echo "Iteration $i/$ITERATIONS"
    echo "========================================"

    echo "[1/2] Running with ProfileReuse + Measure"
    "$JAVA" \
        -XX:ProfileReuseFile="$PROFILE_REUSE_FILE" \
        -XX:ProfileReuseMeasureFile="$PROFILE_MEASURE_FILE" \
        -jar "$JAR"

    echo

    echo "[2/2] Running with Measure only"
    "$JAVA" \
        -XX:ProfileReuseMeasureFile="$PROFILE_MEASURE_FILE" \
        -jar "$JAR"

    echo
done
