#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build-bench"
RESULT_DIR="${ROOT_DIR}/bench/results"
TIMESTAMP="$(date -u +"%Y%m%dT%H%M%SZ")"

BENCHMARK_EXE="${BUILD_DIR}/bench/datagine_benchmarks"
TEXT_RESULT="${RESULT_DIR}/datagine_benchmarks_${TIMESTAMP}.txt"
JSON_RESULT="${RESULT_DIR}/datagine_benchmarks_${TIMESTAMP}.json"
METADATA_RESULT="${RESULT_DIR}/datagine_benchmarks_${TIMESTAMP}_metadata.txt"
TMP_DIR="${BUILD_DIR}/bench-run-${TIMESTAMP}"
TMP_TEXT="${TMP_DIR}/datagine_benchmarks_${TIMESTAMP}.txt"
TMP_JSON="${TMP_DIR}/datagine_benchmarks_${TIMESTAMP}.json"
TMP_METADATA="${TMP_DIR}/datagine_benchmarks_${TIMESTAMP}_metadata.txt"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DDATAGINE_BUILD_BENCHMARKS=ON

cmake --build "${BUILD_DIR}" --target datagine_benchmarks --config Release

mkdir -p "${TMP_DIR}"

{
    echo "timestamp_utc: ${TIMESTAMP}"
    echo "git_commit: $(git -C "${ROOT_DIR}" rev-parse --short HEAD 2>/dev/null || echo unknown)"
    echo "build_type: Release"
    echo "benchmark_target: ${BENCHMARK_EXE}"
    echo
    echo "uname:"
    uname -a || true
    echo
    echo "cpu:"
    if command -v lscpu >/dev/null 2>&1; then
        lscpu
    elif command -v sysctl >/dev/null 2>&1; then
        sysctl -n machdep.cpu.brand_string 2>/dev/null || true
        sysctl -n hw.ncpu 2>/dev/null || true
    else
        echo "cpu metadata unavailable"
    fi
    echo
    echo "memory:"
    if command -v free >/dev/null 2>&1; then
        free -h
    elif command -v sysctl >/dev/null 2>&1; then
        sysctl -n hw.memsize 2>/dev/null || true
    else
        echo "memory metadata unavailable"
    fi
    echo
    echo "compiler:"
    "${CXX:-c++}" --version 2>/dev/null || echo "compiler metadata unavailable"
    echo
    echo "cmake:"
    cmake --version
} > "${TMP_METADATA}"

"${BENCHMARK_EXE}" \
    --benchmark_repetitions=10 \
    --benchmark_report_aggregates_only=false \
    --benchmark_counters_tabular=true \
    --benchmark_out="${TMP_JSON}" \
    --benchmark_out_format=json \
    2>&1 | tee "${TMP_TEXT}"

mkdir -p "${RESULT_DIR}"
mv "${TMP_TEXT}" "${TEXT_RESULT}"
mv "${TMP_JSON}" "${JSON_RESULT}"
mv "${TMP_METADATA}" "${METADATA_RESULT}"

echo "wrote text results: ${TEXT_RESULT}"
echo "wrote json results: ${JSON_RESULT}"
echo "wrote metadata: ${METADATA_RESULT}"
