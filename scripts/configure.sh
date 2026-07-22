#!/usr/bin/env bash
set -euo pipefail
preset="${1:-tests-debug}"
cmake --preset "${preset}" --fresh

