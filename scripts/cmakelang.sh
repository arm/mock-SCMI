#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail
command -v cmake-lint >/dev/null 2>&1 || {
	echo "❌ cmake-lint is not installed" >&2
	exit 1
}

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake-lint --suppress-decorations -l error "$REPO_ROOT/CMakeLists.txt"
echo "✅ CMake lint passed."
