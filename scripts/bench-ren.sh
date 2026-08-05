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

PROFILE_CSV="profilereuse.csv"
VANILLA_CSV="vanilla.csv"
TMP_CSV="$(mktemp)"

# Start with fresh measurement files
: > "$PROFILE_MEASURE_FILE"
: > "$VANILLA_MEASURE_FILE"

# Start with fresh Renaissance CSVs
rm -f "$PROFILE_CSV" "$VANILLA_CSV"

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
        "$BENCHMARK" \
        -r 10 \
        --csv "$TMP_CSV"

    # Append Renaissance CSV (keep header only once)
    if [[ ! -f "$PROFILE_CSV" ]]; then
        cat "$TMP_CSV" > "$PROFILE_CSV"
    else
        tail -n +2 "$TMP_CSV" >> "$PROFILE_CSV"
    fi

    echo

    echo "[2/2] Vanilla"

    /usr/bin/time -f "%e" \
        -o vanilla_runtime.data \
        -a \
        "$JAVA" \
        -XX:ProfileReuseMeasureFile="$VANILLA_MEASURE_FILE" \
        -jar "$RENAISSANCE_JAR" \
        "$BENCHMARK" \
        -r 10 \
        --csv "$TMP_CSV"

    # Append Renaissance CSV (keep header only once)
    if [[ ! -f "$VANILLA_CSV" ]]; then
        cat "$TMP_CSV" > "$VANILLA_CSV"
    else
        tail -n +2 "$TMP_CSV" >> "$VANILLA_CSV"
    fi

    echo
done

rm -f "$TMP_CSV"
