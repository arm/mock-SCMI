#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail
command -v reuse >/dev/null 2>&1 || {
	echo "❌ reuse is not installed" >&2
	exit 1
}
command -v jq >/dev/null 2>&1 || {
	echo "❌ jq is not installed" >&2
	exit 1
}
reuse --help 2>&1 | grep -q REUSE.toml || {
	echo "❌ REUSE 3.3 or newer is required" >&2
	exit 2
}

set +e
result="$(reuse lint -j)"
status=$?
set -e
jq -e . >/dev/null <<<"$result" || exit "${status:-2}"

too_many="$(jq -r '.files[] | select((.copyrights // []) | length > 2) | .path' <<<"$result")"
missing="$(jq -r '(.non_compliant.missing_licensing_info // []) + (.non_compliant.missing_copyright_info // []) | unique | .[]' <<<"$result")"
[[ -z $too_many ]] || {
	echo "❌ Files with more than two copyright notices:"
	echo "$too_many"
	exit 3
}
[[ -z $missing ]] || {
	echo "❌ Files missing licensing metadata:"
	echo "$missing"
	exit 4
}
[[ $status -eq 0 ]] || exit "$status"
echo "✅ REUSE license validation passed."
