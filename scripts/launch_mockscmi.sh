#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MOCK_SCMI_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
echo "MOCK_SCMI_ROOT = $MOCK_SCMI_ROOT"

########################################
# Launch MockScmi (FUSE) demo          #
########################################
DEFAULT_TLM_JSON="$MOCK_SCMI_ROOT/config/tlm.json"
: "${MOCK_SCMI_TLM_JSON_PATH:=$DEFAULT_TLM_JSON}"
export MOCK_SCMI_TLM_JSON_PATH
echo "MOCK_SCMI_TLM_JSON_PATH = $MOCK_SCMI_TLM_JSON_PATH"

# Select the MockScmi interface. Keep sysfs as the default for backwards
# compatibility with existing callers of this script.
: "${MOCK_SCMI_INTERFACE:=sysfs}"
MOCK_SCMI_INTERFACE="${MOCK_SCMI_INTERFACE,,}"
case "$MOCK_SCMI_INTERFACE" in
auto | sysfs | ioctl) ;;
*)
	echo "❌ MOCK_SCMI_INTERFACE must be one of: auto, sysfs, ioctl" >&2
	exit 1
	;;
esac
export MOCK_SCMI_INTERFACE
echo "MOCK_SCMI_INTERFACE = $MOCK_SCMI_INTERFACE"

MOCK_SCMI="${MOCK_SCMI_BIN:-$MOCK_SCMI_ROOT/build/MockScmi}"
MOCK_SCMI_CUSE="${MOCK_SCMI_CUSE_BIN:-$MOCK_SCMI_ROOT/build/MockScmiCuse}"
echo "MOCK_SCMI = $MOCK_SCMI"
echo "MOCK_SCMI_CUSE = $MOCK_SCMI_CUSE"

# Allow optional arguments to override MOUNT_POINT and SCMI_LOG.
MOUNT_POINT="$HOME/tmp/mock-scmi"
SCMI_LOG="$MOCK_SCMI_ROOT/mock_scmi.log"
if [[ $# -gt 2 ]]; then
	echo "❌ Usage: $(basename "$0") [MOUNT_POINT] [SCMI_LOG]" >&2
	exit 1
fi
if [[ $# -ge 1 ]]; then
	MOUNT_POINT="$1"
	if [[ ! -d $MOUNT_POINT ]]; then
		echo "📁 Mount point '$MOUNT_POINT' does not exist, creating it..."
		mkdir -p "$MOUNT_POINT" || {
			echo "❌ Failed to create mount point '$MOUNT_POINT'" >&2
			exit 1
		}
	fi
fi
if [[ $# -eq 2 ]]; then
	SCMI_LOG="$2"
fi

# Constants for startup detection
TIMEOUT=30
PATTERN_READY="eccf4f7c-d1b1-47f0-9d23-159f6d38b661"

# Basic sanity checks
if [[ $MOCK_SCMI_INTERFACE == "ioctl" ]]; then
	[[ -x $MOCK_SCMI_CUSE ]] || {
		echo "❌ MockScmiCuse not found or not executable at $MOCK_SCMI_CUSE" >&2
		exit 1
	}
else
	[[ -x $MOCK_SCMI ]] || {
		echo "❌ MockScmi not found or not executable at $MOCK_SCMI" >&2
		exit 1
	}
fi

mkdir -p "$MOUNT_POINT"
MOUNT_POINT="$(realpath "$MOUNT_POINT")"
PID_FILE="${MOUNT_POINT}.mock_scmi.pid"

if compgen -G "$MOUNT_POINT/tlm_*" >/dev/null || mountpoint -q "$MOUNT_POINT"; then
	echo "❌ A MockScmi interface already exists at $MOUNT_POINT" >&2
	echo "Run the cleanup command before launching another instance." >&2
	exit 1
fi

if [[ -f $PID_FILE ]]; then
	while read -r OLD_PID; do
		if [[ $OLD_PID =~ ^[0-9]+$ ]] && ps -p "$OLD_PID" >/dev/null; then
			echo "❌ MockScmi is already recorded at $MOUNT_POINT with PID $OLD_PID" >&2
			echo "Run the cleanup command before launching another instance." >&2
			exit 1
		fi
	done <"$PID_FILE"
	rm -f "$PID_FILE"
fi

echo "Logs Directory = $SCMI_LOG"

echo "🚀 Launching MockScmi..."
: >"$SCMI_LOG"
SCMI_PIDS=()
CUSE_DEVICE_PATHS=()

if [[ $MOCK_SCMI_INTERFACE == "ioctl" ]]; then
	if [[ ! -e /dev/cuse ]]; then
		echo "❌ /dev/cuse is unavailable. Load the kernel module with: sudo modprobe cuse" >&2
		exit 1
	fi

	CUSE_RUNNER=()
	if [[ ! -r /dev/cuse || ! -w /dev/cuse ]]; then
		if ! command -v sudo >/dev/null; then
			echo "❌ /dev/cuse requires root access and sudo is unavailable" >&2
			exit 1
		fi
		echo "🔐 CUSE device creation requires elevated access"
		if [[ ${MOCK_SCMI_SUDO_NONINTERACTIVE:-0} == 1 ]]; then
			if ! sudo -n true; then
				echo "❌ CUSE test needs cached sudo credentials. Run 'sudo -v' first." >&2
				exit 77
			fi
			CUSE_RUNNER=(sudo -n)
		else
			sudo -v
			CUSE_RUNNER=(sudo)
		fi
	fi

	mapfile -t TLM_TARGETS < <("$MOCK_SCMI_CUSE" --list-targets --tlm-json-path "$MOCK_SCMI_TLM_JSON_PATH")
	if ((${#TLM_TARGETS[@]} == 0)); then
		echo "❌ No telemetry targets found in $MOCK_SCMI_TLM_JSON_PATH" >&2
		exit 1
	fi

	for TARGET_ID in "${TLM_TARGETS[@]}"; do
		DEVICE_BASENAME="${TARGET_ID/-/_}"
		CUSE_DEVICE_NAME="mock_scmi_${$}_${DEVICE_BASENAME}"
		CUSE_DEVICE_PATH="/dev/$CUSE_DEVICE_NAME"
		nohup "${CUSE_RUNNER[@]}" "$MOCK_SCMI_CUSE" --target "$TARGET_ID" --device-name "$CUSE_DEVICE_NAME" \
			--tlm-json-path "$MOCK_SCMI_TLM_JSON_PATH" >>"$SCMI_LOG" 2>&1 &
		SCMI_PIDS+=("$!")
		CUSE_DEVICE_PATHS+=("$CUSE_DEVICE_PATH")
	done
else
	# Keep the sysfs/FUSE process alive after this launcher script exits.
	nohup "$MOCK_SCMI" -f -s --tlm-json-path "$MOCK_SCMI_TLM_JSON_PATH" "$MOUNT_POINT" >>"$SCMI_LOG" 2>&1 &
	SCMI_PIDS+=("$!")
fi
printf '%s\n' "${SCMI_PIDS[@]}" >"$PID_FILE"

LAUNCH_COMPLETE=0
cleanup_failed_launch() {
	local status=$?
	if ((status != 0 && LAUNCH_COMPLETE == 0)); then
		for PID in "${SCMI_PIDS[@]}"; do
			kill -SIGTERM "$PID" 2>/dev/null || sudo kill -SIGTERM "$PID" 2>/dev/null || true
			wait "$PID" 2>/dev/null || true
		done
		rm -f "$PID_FILE"
	fi
}
trap cleanup_failed_launch EXIT

# Note, if not mockscmi, use
# `	mount -t stlmfs none /sys/fs/arm_telemetry/ `
# to mount the real sysfs interface

########################################
# Helper: wait for GUID in the log     #
########################################
wait_for() {
	local file=$1 desc=$2 pattern=$3
	echo "⏱️  Waiting (up to ${TIMEOUT}s) for '$pattern' in $desc..."
	timeout "$TIMEOUT" bash -c \
		"stdbuf -oL tail -n +0 -F '$file' | grep -m1 -F '$pattern'" ||
		{
			echo '❌ Timeout waiting for MockScmi' >&2
			exit 1
		}
	echo "✅ Detected '$pattern' in $desc"
}

if [[ $MOCK_SCMI_INTERFACE == "ioctl" ]]; then
	for INDEX in "${!CUSE_DEVICE_PATHS[@]}"; do
		CUSE_DEVICE_PATH="${CUSE_DEVICE_PATHS[$INDEX]}"
		SCMI_PID="${SCMI_PIDS[$INDEX]}"
		for ((ATTEMPT = 0; ATTEMPT < TIMEOUT * 10; ++ATTEMPT)); do
			[[ -e $CUSE_DEVICE_PATH ]] && break
			if ! ps -p "$SCMI_PID" >/dev/null; then
				echo "❌ MockScmiCuse exited before creating $CUSE_DEVICE_PATH. See $SCMI_LOG" >&2
				exit 1
			fi
			sleep 0.1
		done
		if [[ ! -e $CUSE_DEVICE_PATH ]]; then
			echo "❌ Timed out waiting for CUSE device $CUSE_DEVICE_PATH" >&2
			exit 1
		fi
		if [[ ! -r $CUSE_DEVICE_PATH || ! -w $CUSE_DEVICE_PATH ]]; then
			"${CUSE_RUNNER[@]}" chmod a+rw "$CUSE_DEVICE_PATH"
		fi
		TARGET_ID="${TLM_TARGETS[INDEX]}"
		DEVICE_BASENAME="${TARGET_ID/-/_}"
		ln -sfn "$CUSE_DEVICE_PATH" "$MOUNT_POINT/$DEVICE_BASENAME"

		for CANDIDATE_PID in $(pgrep -x MockScmiCuse || true); do
			COMMAND_LINE="$(tr '\0' ' ' <"/proc/$CANDIDATE_PID/cmdline" 2>/dev/null || true)"
			if [[ $COMMAND_LINE == *"--device-name $(basename "$CUSE_DEVICE_PATH")"* ]]; then
				SCMI_PIDS[INDEX]="$CANDIDATE_PID"
				break
			fi
		done
	done
	printf '%s\n' "${SCMI_PIDS[@]}" >"$PID_FILE"
else
	wait_for "$SCMI_LOG" "MockScmi startup log" "$PATTERN_READY"
fi

echo "✅ MockScmi mounted at $MOUNT_POINT"
if [[ $MOCK_SCMI_INTERFACE == "ioctl" ]]; then
	TLM_DEVICE="$MOUNT_POINT/tlm_0"
	if [[ ! -r $TLM_DEVICE || ! -w $TLM_DEVICE ]]; then
		echo "❌ MockScmi mounted, but ioctl device $TLM_DEVICE is not both readable and writable" >&2
		exit 1
	fi
	echo "✅ Verified ioctl device: $TLM_DEVICE"
else
	# Verify that the mounted sysfs interface accepts control writes.
	TLM_ENABLE_FILE="$MOUNT_POINT/arm_telemetry/tlm-0/tlm_enable"
	if ! printf '1' >"$TLM_ENABLE_FILE"; then
		echo "❌ MockScmi mounted, but $TLM_ENABLE_FILE is not writable" >&2
		exit 1
	fi
	echo "✅ Verified writable control file: $TLM_ENABLE_FILE"
fi
echo "🧵 To stop MockScmi (PIDs=${SCMI_PIDS[*]})..."
echo "Run: just cleanup $MOUNT_POINT"
LAUNCH_COMPLETE=1
