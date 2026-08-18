#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail

for tool in clang-format cmake-format; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "❌ $tool is not installed" >&2
		exit 1
	fi
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/utils.sh"
get_all_source_files

clang-format --dry-run --Werror "${SOURCE_FILES[@]}"
cmake-format --check "$SCRIPT_DIR/../CMakeLists.txt"
echo "✅ Source and CMake formatting is correct."
