#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail

mode=all
case "${1:-}" in
"") ;;
--staged) mode=staged ;;
*)
	echo "Usage: $0 [--staged]" >&2
	exit 2
	;;
esac
[[ $# -le 1 ]] || {
	echo "Usage: $0 [--staged]" >&2
	exit 2
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

for tool in clang-format cmake-format; do
	if ! command -v "$tool" >/dev/null 2>&1; then
		echo "❌ $tool is not installed" >&2
		exit 1
	fi
done

STAGED_FILES=()
if [[ $mode == staged ]]; then
	while IFS= read -r -d '' file; do STAGED_FILES+=("$file"); done \
		< <(git diff --cached --name-only --diff-filter=ACMR -z)
fi

# shellcheck disable=SC1091
source "$SCRIPT_DIR/utils.sh"
get_all_source_files
clang-format -i "${SOURCE_FILES[@]}"
cmake-format -i CMakeLists.txt

if command -v qlty >/dev/null 2>&1; then
	qlty fmt ./scripts/ ./kernel_tests/ ./*.md ./.github/
else
	echo "⚠️  qlty is unavailable; shell, Markdown, and YAML auto-formatting was skipped" >&2
fi

if [[ $mode == staged ]]; then
	for file in "${STAGED_FILES[@]+"${STAGED_FILES[@]}"}"; do
		[[ -e $file ]] && git add -- "$file"
	done
fi
