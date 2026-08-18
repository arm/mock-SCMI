#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail
[[ $# -eq 1 ]] || {
	echo "Usage: $0 <build-dir>" >&2
	exit 2
}
command -v cppcheck >/dev/null 2>&1 || {
	echo "❌ cppcheck is not installed" >&2
	exit 1
}

BUILD_DIR="$(realpath "$1")"
COMPILE_COMMANDS="$BUILD_DIR/compile_commands.json"
[[ -f $COMPILE_COMMANDS ]] || {
	echo "❌ $COMPILE_COMMANDS is missing; configure with CMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
	exit 1
}

cppcheck -v --project="$COMPILE_COMMANDS" --std=c++20 --inline-suppr --enable=all \
	--suppress=unusedFunction \
	--suppress=syntaxError \
	--suppress=unknownMacro \
	--suppress=unmatchedSuppression \
	--suppress=missingInclude \
	--suppress=missingIncludeSystem \
	--suppress=normalCheckLevelMaxBranches \
	--suppress='*:*build/*' \
	--quiet --error-exitcode=1
