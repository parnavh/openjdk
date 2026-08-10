#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <iterations> <renaissance_jar>"
    exit 1
fi

ITERATIONS="$1"
RENAISSANCE_JAR="$2"

JAVA="./build/linux-x86_64-server-release/jdk/bin/java"

RUNS_DIR="runs"
FAIL_FILE="$RUNS_DIR/fail"
TMP_CSV="$(mktemp)"

trap 'rm -f "$TMP_CSV"' EXIT

BENCHMARKS=(
    akka-uct
    als
    chi-square
    db-shootout
    dec-tree
    dotty
    finagle-chirper
    finagle-http
    fj-kmeans
    future-genetic
    # gauss-mix
    log-regression
    mnemonics
    movie-lens
    naive-bayes
    neo4j-analytics
    page-rank
    par-mnemonics
    philosophers
    reactors
    rx-scrabble
    scala-doku
    scala-kmeans
    scala-stm-bench7
    scrabble
)

mkdir -p "$RUNS_DIR"
touch "$FAIL_FILE"

for BENCHMARK in "${BENCHMARKS[@]}"; do
    echo "########################################"
    echo "Benchmark: $BENCHMARK"
    echo "########################################"

    BENCHMARK_DIR="$RUNS_DIR/$BENCHMARK"

    mkdir -p "$BENCHMARK_DIR"

    PROFILE_FILE="$BENCHMARK_DIR/profile.data"
    PROFILE_MEASURE_FILE="$BENCHMARK_DIR/measurement_profile.tsv"
    VANILLA_MEASURE_FILE="$BENCHMARK_DIR/measurement_vanilla.tsv"

    PROFILE_TIME="$BENCHMARK_DIR/profile.time"
    VANILLA_TIME="$BENCHMARK_DIR/vanilla.time"

    PROFILE_CSV="$BENCHMARK_DIR/profile.csv"
    VANILLA_CSV="$BENCHMARK_DIR/vanilla.csv"

    # Start with fresh measurement/time/CSV files.
    : > "$PROFILE_MEASURE_FILE"
    : > "$VANILLA_MEASURE_FILE"
    : > "$PROFILE_TIME"
    : > "$VANILLA_TIME"

    rm -f "$PROFILE_CSV" "$VANILLA_CSV"

    BENCHMARK_FAILED=0

    for ((i=1; i<=ITERATIONS; i++)); do
        echo "========================================"
        echo "$BENCHMARK: Iteration $i/$ITERATIONS"
        echo "========================================"

        echo "[1/2] ProfileReuse"

        set +e

        /usr/bin/time -f "%e" \
            -o "$PROFILE_TIME" \
            -a \
            "$JAVA" \
            -XX:ProfileReuseFile="$PROFILE_FILE" \
            -XX:ProfileReuseMeasureFile="$PROFILE_MEASURE_FILE" \
            -jar "$RENAISSANCE_JAR" \
            "$BENCHMARK" \
            -r 10 \
            --csv "$TMP_CSV"

        PROFILE_STATUS=$?

        set -e

        if [[ "$PROFILE_STATUS" -ne 0 ]]; then
            BENCHMARK_FAILED=1
            break
        fi

        # Append Renaissance CSV, keeping the header only once.
        if [[ ! -f "$PROFILE_CSV" ]]; then
            cat "$TMP_CSV" > "$PROFILE_CSV"
        else
            tail -n +2 "$TMP_CSV" >> "$PROFILE_CSV"
        fi

        echo

        echo "[2/2] Vanilla"

        set +e

        /usr/bin/time -f "%e" \
            -o "$VANILLA_TIME" \
            -a \
            "$JAVA" \
            -XX:ProfileReuseMeasureFile="$VANILLA_MEASURE_FILE" \
            -jar "$RENAISSANCE_JAR" \
            "$BENCHMARK" \
            -r 10 \
            --csv "$TMP_CSV"

        VANILLA_STATUS=$?

        set -e

        if [[ "$VANILLA_STATUS" -ne 0 ]]; then
            BENCHMARK_FAILED=1
            break
        fi

        # Append Renaissance CSV, keeping the header only once.
        if [[ ! -f "$VANILLA_CSV" ]]; then
            cat "$TMP_CSV" > "$VANILLA_CSV"
        else
            tail -n +2 "$TMP_CSV" >> "$VANILLA_CSV"
        fi

        echo
    done

    if [[ "$BENCHMARK_FAILED" -eq 1 ]]; then
        echo "$BENCHMARK" >> "$FAIL_FILE"
        echo "Failed: $BENCHMARK"
    else
        echo "Completed: $BENCHMARK"
    fi

    echo
done

echo "All benchmarks completed."
