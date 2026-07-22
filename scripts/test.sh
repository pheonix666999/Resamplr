#!/usr/bin/env bash
set -euo pipefail
preset="${1:-tests-debug}"
ctest --preset "${preset}" --output-on-failure --no-tests=error
