#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail

SOURCE_FILES=()

get_all_source_files() {
	local repo_root
	repo_root="$(git rev-parse --show-toplevel 2>/dev/null || realpath .)"
	while IFS= read -r -d '' file; do
		SOURCE_FILES+=("$file")
	done < <(find "$repo_root/include" "$repo_root/src" "$repo_root/kernel_tests" -type f \
		\( -name '*.c' -o -name '*.cpp' -o -name '*.h' -o -name '*.hpp' -o -name '*.hxx' \) -print0)
	export SOURCE_FILES
}
