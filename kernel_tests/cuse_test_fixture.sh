#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SKIP_CODE=77
if [[ $# -ne 3 ]]; then
	echo "Usage: $0 <MockScmiCuse> <ioctl-smoke-test> <expected-output>" >&2
	exit 2
fi

CUSE_EXECUTABLE="$(realpath "$1")"
SMOKE_TEST="$(realpath "$2")"
EXPECTED_OUTPUT="$(realpath "$3")"
TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MOCK_SCMI_ROOT="$(realpath "$TEST_DIR/..")"

if [[ ! -e /dev/cuse ]]; then
	echo "Skipping CUSE ioctl test: /dev/cuse is unavailable"
	exit "$SKIP_CODE"
fi
if [[ ! -r /dev/cuse || ! -w /dev/cuse ]]; then
	if ! command -v sudo >/dev/null || ! sudo -n true; then
		echo "Skipping CUSE ioctl test: run 'sudo -v' first to authorize CUSE device creation"
		exit "$SKIP_CODE"
	fi
fi

TMP_DIR="$(mktemp -d /tmp/mock_scmi_cuse_test.XXXXXXXX)"
MOUNT_POINT="$TMP_DIR/root"
SCMI_LOG="$TMP_DIR/mock_scmi.log"
ACTUAL_OUTPUT="$TMP_DIR/actual.txt"
mkdir "$MOUNT_POINT"

cleanup() {
	"$MOCK_SCMI_ROOT/scripts/cleanup_mockscmi.sh" "$MOUNT_POINT" >/dev/null 2>&1 || true
	rm -rf "$TMP_DIR"
}
trap cleanup EXIT

MOCK_SCMI_INTERFACE=ioctl \
	MOCK_SCMI_CUSE_BIN="$CUSE_EXECUTABLE" \
	MOCK_SCMI_TLM_JSON_PATH="$MOCK_SCMI_ROOT/config/tlm.json" \
	MOCK_SCMI_SUDO_NONINTERACTIVE=1 \
	"$MOCK_SCMI_ROOT/scripts/launch_mockscmi.sh" "$MOUNT_POINT" "$SCMI_LOG"

export TEST_SCRATCH="$TMP_DIR/scratch"
mkdir "$TEST_SCRATCH"
(
	cd "$MOUNT_POINT"
	bash "$TEST_DIR/ioctl_backend/command.sh" "$SMOKE_TEST"
) >"$ACTUAL_OUTPUT" 2>&1

diff -u "$EXPECTED_OUTPUT" "$ACTUAL_OUTPUT"
echo "CUSE ioctl smoke test passed"
