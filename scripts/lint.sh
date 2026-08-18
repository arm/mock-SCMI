#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail
[[ $# -ge 1 && $# -le 2 ]] || {
	echo "Usage: $0 <build-dir> [pre-commit|pull-request|all]" >&2
	exit 2
}
command -v clang-tidy >/dev/null 2>&1 || {
	echo "❌ clang-tidy is not installed" >&2
	exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$(realpath "$1")"
MODE="${2:-pull-request}"
[[ -f $BUILD_DIR/compile_commands.json ]] || {
	echo "❌ $BUILD_DIR/compile_commands.json is missing" >&2
	exit 1
}

# shellcheck disable=SC1091
source "$SCRIPT_DIR/utils.sh"
get_all_source_files

FILES=()
SHELL_FILES=()
JUSTFILES=()
case "$MODE" in
all)
	FILES=("${SOURCE_FILES[@]}")
	while IFS= read -r file; do FILES+=("$file"); done < <(git ls-files '*.sh' justfile)
	;;
pre-commit)
	while IFS= read -r file; do [[ -n $file ]] && FILES+=("$file"); done \
		< <(git diff --cached --name-only --diff-filter=ACM | grep -E '\.(cpp|cc|cxx|c|h|hpp|hxx|sh)$|(^|/)justfile$' || true)
	;;
pull-request)
	base_ref="${BASE_REF:-main}"
	head_ref="${HEAD_REF:-HEAD}"
	git rev-parse --verify --quiet "origin/$base_ref^{commit}" >/dev/null && base_ref="origin/$base_ref"
	while IFS= read -r file; do [[ -n $file ]] && FILES+=("$file"); done \
		< <(git diff "$base_ref...$head_ref" --name-only --diff-filter=ACM | grep -E '\.(cpp|cc|cxx|c|h|hpp|hxx|sh)$|(^|/)justfile$' || true)
	;;
*)
	echo "Unknown mode: $MODE" >&2
	exit 2
	;;
esac

INCLUDE_ARGS=(-I"$REPO_ROOT/include" -I"$BUILD_DIR/_deps/nlohmann_json-src/include" -DFUSE_USE_VERSION=316 -std=c++20)
while IFS= read -r flag; do [[ -n $flag ]] && INCLUDE_ARGS+=("$flag"); done \
	< <(pkg-config --cflags-only-I fuse3 | tr ' ' '\n')
while IFS= read -r system_include; do
	[[ -n $system_include ]] && INCLUDE_ARGS+=(-isystem "$system_include")
done < <(echo | g++ -E -x c++ - -v 2>&1 |
	awk '/#include <...> search starts here:/{flag=1;next}/End of search list/{flag=0}flag' |
	sed -E 's/^[[:space:]]+//')

for file in "${FILES[@]+"${FILES[@]}"}"; do
	[[ -e $file ]] || continue
	case "$file" in
	*.sh) SHELL_FILES+=("$file") ;;
	*justfile) JUSTFILES+=("$file") ;;
	*.cpp | *.cc | *.cxx | *.hpp | *.hxx | *.h)
		checks=""
		[[ $file == */kernel_tests/* || $file == kernel_tests/* ]] &&
			checks="-cppcoreguidelines-avoid-magic-numbers,-readability-magic-numbers,-readability-function-cognitive-complexity"
		args=(-p "$BUILD_DIR" --warnings-as-errors=*)
		[[ -z $checks ]] || args+=(-checks "$checks")
		clang-tidy "$file" "${args[@]}" -- "${INCLUDE_ARGS[@]}"
		;;
	esac
done

if [[ ${#SHELL_FILES[@]} -gt 0 ]]; then
	if command -v shellcheck >/dev/null 2>&1; then shellcheck "${SHELL_FILES[@]}"; else
		for file in "${SHELL_FILES[@]}"; do bash -n "$file"; done
	fi
fi
if [[ ${#JUSTFILES[@]} -gt 0 ]] && command -v just >/dev/null 2>&1; then
	just --justfile "$REPO_ROOT/justfile" --dump >/dev/null
fi
echo "✅ Linting completed successfully."
