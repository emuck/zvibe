#!/bin/bash
# Copyright (c) 2025 Martin R. Raumann
# SPDX-License-Identifier: BSD-3-Clause
# Thin wrapper — delegates to test_script.py
# Usage:
#   ./test_script.sh                  # restaurant walkthrough + stress (game ships with repo)
#   ./test_script.sh plunderedhearts  # download and test Plundered Hearts
#   ./test_script.sh seastalker       # download and test Seastalker
set -e
exec python3 "$(dirname "$0")/test_script.py" "$@"
