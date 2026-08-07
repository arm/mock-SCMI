#!/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

# This script is a wrapper for Sysfs unit tests
# It launches a MockScmi executable at a unique mount point
# then runs whatever command is passed as an argument

set -eu -o pipefail

# --- Configure env for MockScmi JSON path ---

set -x
# Script path: astl/tools/mock_scmi/unit_tests/unit_test_fixture.sh
# Repo root is three directories up from here.
MSYS_UNIT_TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASTL_ROOT="$(realpath "${MSYS_UNIT_TEST_DIR}/../../..")"
DEFAULT_TLM_JSON="${ASTL_ROOT}/tools/mock_scmi/config/tlm.json"
: "${ASTL_MOCKSCMI_TLM_JSON_PATH:=${DEFAULT_TLM_JSON}}"
export ASTL_MOCKSCMI_TLM_JSON_PATH
if [[ ! -f ${ASTL_MOCKSCMI_TLM_JSON_PATH} ]]; then
	echo "❌ ASTL_MOCKSCMI_TLM_JSON_PATH not found: ${ASTL_MOCKSCMI_TLM_JSON_PATH}" >&2
	exit 1
fi
echo "✅ ASTL_MOCKSCMI_TLM_JSON_PATH = ${ASTL_MOCKSCMI_TLM_JSON_PATH}"

TIMEOUT=30 # Maximum seconds to wait for guid
PATTERN_READY="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"
SKIP_CODE=77

if [[ ! -e /dev/fuse || ! -r /dev/fuse || ! -w /dev/fuse ]]; then
	echo "⚠️  Skipping MockScmi test: /dev/fuse is unavailable or lacks rw permissions"
	exit "${SKIP_CODE}"
fi

if [ $# -lt 2 ]; then
	echo "❌ Error!  You must pass one or more commands to this script to run" >/dev/stderr
	echo "Usage: $0 <sysfs_executable> <expected_output> <command> [arg1] [arg2] ... [argN]" >/dev/stderr
	exit 1
fi

SYSFS_EXECUTABLE=$(realpath "$1")
shift
EXPECTED_OUTPUT=$(realpath "$1")
shift

if [ -f "${SYSFS_EXECUTABLE}" ]; then
	echo "✅ Found MockScmi binary at: ${SYSFS_EXECUTABLE}"
else
	echo "❌ Error!  Sysfs binary not found at: ${SYSFS_EXECUTABLE}" >/dev/stderr
	echo "Note: Did you forget to build it with CMake?" >/dev/stderr
	exit 1
fi

TMP_DIR=$(mktemp -d /tmp/sysfs_unit_test_XXXXXXXX)
if [ -d "${TMP_DIR}" ]; then
	echo "✅ Created temporary directory at: ${TMP_DIR}"
else
	echo "❌ Error!  Failed to create empty mount point" >/dev/stderr
	exit 1
fi

SYSFS_LOG="${TMP_DIR}/sysfs_log.txt" # Combined stdout and stderr from MockScmi process (and Valgrind)
VALGRIND_LOG="${TMP_DIR}/valgrind_log.txt"
MOUNT_POINT="${TMP_DIR}/mount" # Directory where sysfs will be mounted
ACTUAL_OUTPUT="${TMP_DIR}/actual.txt"
export TEST_SCRATCH="${TMP_DIR}/scratch"

mkdir "${MOUNT_POINT}" "${TEST_SCRATCH}"

wait_for() {
	local FILE="$1" DESC="$2" PATTERN="$3"
	echo "⏱️  Waiting (up to ${TIMEOUT}s) for '$PATTERN' in ${DESC}..."
	timeout "${TIMEOUT}" bash -c "
        stdbuf -oL tail -n +0 -F \"${FILE}\" | grep -m1 -F \"${PATTERN}\"
    "
	echo "✅ Detected '$PATTERN' in ${DESC}"
}

# If valgrind reports an "unhandled dwarf2 abbrev form code 0x25" error, try compiling with g++ instead of clang
# clear DEBUGINFOD_URLS so that valgrind doesn't attempt to fetch debug symbols from the network, which can cause hangs in constrained environments
DEBUGINFOD_URLS='' valgrind --log-file="${VALGRIND_LOG}" --leak-check=full --show-leak-kinds=all --track-origins=yes "${SYSFS_EXECUTABLE}" -f -s "${MOUNT_POINT}" &>"${SYSFS_LOG}" &
#{SYSFS_EXECUTABLE} -f -s ${MOUNT_POINT} &> ${SYSFS_LOG} &
SYSFS_PROCESS="$!"

if ps -p ${SYSFS_PROCESS} >/dev/null; then
	echo "✅ MockScmi process launched with PID = ${SYSFS_PROCESS} and log ${SYSFS_LOG}"
else
	echo "❌ Error!  Failed to launch MockScmi process" >/dev/stderr
	exit 1
fi

if ! wait_for "${SYSFS_LOG}" "MockScmi startup" "${PATTERN_READY}"; then
	# In constrained environments, FUSE is present but cannot be used.
	# Treat this as a skipped test instead of a hard failure.
	if grep -qF "failed to open /dev/fuse: Permission denied" "${SYSFS_LOG}"; then
		echo "⚠️  Skipping MockScmi test: /dev/fuse permission denied in this environment"
		exit "${SKIP_CODE}"
	fi
	echo "❌ Error!  MockScmi startup pattern was not detected. See log: ${SYSFS_LOG}" >/dev/stderr
	exit 1
fi

if ! ps -p ${SYSFS_PROCESS} >/dev/null; then
	if grep -qF "failed to open /dev/fuse: Permission denied" "${SYSFS_LOG}"; then
		echo "⚠️  Skipping MockScmi test: process exited after startup due /dev/fuse permission denial"
		exit "${SKIP_CODE}"
	fi
	echo "❌ Error!  MockScmi process exited unexpectedly after startup. See log: ${SYSFS_LOG}" >/dev/stderr
	exit 1
fi

echo 'Start of test output...'

# Launch whatever command we want to use as our unit test
cd "${MOUNT_POINT}"
COMMAND_STATUS=0
"$@" |& tee "${ACTUAL_OUTPUT}" || COMMAND_STATUS=$?
if ((COMMAND_STATUS != 0)); then
	kill -SIGTERM "${SYSFS_PROCESS}" 2>/dev/null || true
	wait "${SYSFS_PROCESS}" || true
	rm -rf "${TMP_DIR}"
	exit "${COMMAND_STATUS}"
fi

# Give valgrind time to flushs its output.
# Without this, the `wait` below will hang because the `kill` command doesn't really terminate the process and/or its children.
# A value of less than 0.5 seconds seems to be too short, as measured experimentally.
sleep 0.5

if kill -SIGTERM ${SYSFS_PROCESS}; then
	echo "✅ Sent SIGTERM sysfs process to gracefully kill it"
else
	if grep -qF "failed to open /dev/fuse: Permission denied" "${SYSFS_LOG}"; then
		echo "⚠️  Skipping MockScmi test: process could not be terminated because /dev/fuse is denied"
		exit "${SKIP_CODE}"
	fi
	echo "❌ Error!  Failed to send kill signal to sysfs process with PID: ${SYSFS_PROCESS}" >/dev/stderr
	exit 1
fi

echo '...End of test output'

# We need an "&& true" guard because wait will pass through the exit code from MockScmi
# which will be 143 when the process ends with SIGTERM, even if there's no other error
wait "${SYSFS_PROCESS}" && true

if grep -F 'ERROR SUMMARY' "${VALGRIND_LOG}" >/dev/null; then
	echo "✅ Valgrind log appears complete"
else
	echo "❌ Error!  Valgrind log was not fully flushed after process ${SYSFS_PROCESS} was killed.  See log: ${VALGRIND_LOG}" >/dev/stderr
	exit 1
fi

if grep -F 'All heap blocks were freed -- no leaks are possible' "${VALGRIND_LOG}" >/dev/null; then
	echo "✅ Valgrind did not detect any memory leaks at runtime"
else
	echo "❌ Error!  Valgrind detected issues.  See log: ${VALGRIND_LOG}" >/dev/stderr
	exit 1
fi

if diff -u "${EXPECTED_OUTPUT}" "${ACTUAL_OUTPUT}"; then
	echo "✅ Actual output matches expected output.  Actual output deleted because test was successful.  Expected output = ${EXPECTED_OUTPUT}"
else
	echo "❌ Error!  Actual test output does not match expected output.  Actual output = ${ACTUAL_OUTPUT}  Expected output = ${EXPECTED_OUTPUT}" >/dev/stderr
	exit 1
fi

rm -rf "${TMP_DIR}"
if [ ! -d "${TMP_DIR}" ]; then
	echo "✅ Cleaned up temporary directory: ${TMP_DIR}"
else
	echo "❌ Error!  Temporary directory not fully removed: ${TMP_DIR}" >/dev/stderr
	exit 1
fi

echo "✅ Reached end of unit test fixture"
