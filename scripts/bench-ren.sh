#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 5 ]; then
    echo "Usage: $0 <iterations> <profile_reuse_file> <profile_measure_file> <vanilla_measure_file> <renaissance_jar>"
    exit 1
fi

ITERATIONS="$1"
PROFILE_REUSE_FILE="$2"
PROFILE_MEASURE_FILE="$3"
VANILLA_MEASURE_FILE="$4"
RENAISSANCE_JAR="$5"

JAVA="./build/linux-x86_64-server-release/jdk/bin/java"
BENCHMARK="gauss-mix"

# Start with fresh measurement files
: > "$PROFILE_MEASURE_FILE"
: > "$VANILLA_MEASURE_FILE"

for ((i=1; i<=ITERATIONS; i++)); do
    echo "========================================"
    echo "Iteration $i/$ITERATIONS"
    echo "========================================"

    echo "[1/2] ProfileReuse"

    /usr/bin/time -f "%e" \
    	-o profile_runtime.data \
    	-a \
    	"$JAVA" \
    	-XX:ProfileReuseFile="$PROFILE_REUSE_FILE" \
    	-XX:ProfileReuseMeasureFile="$PROFILE_MEASURE_FILE" \
    	-jar "$RENAISSANCE_JAR" \
    	"$BENCHMARK"

    echo

    echo "[2/2] Vanilla"

    /usr/bin/time -f "%e" \
    	-o vanilla_runtime.data \
    	-a \
    	"$JAVA" \
    	-XX:ProfileReuseMeasureFile="$VANILLA_MEASURE_FILE" \
    	-jar "$RENAISSANCE_JAR" \
    	"$BENCHMARK"

    echo
done
