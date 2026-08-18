#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

########################################
# cleanup_mockscmi.sh
# Stop MockScmi and cleanup mount point
########################################

MOUNT_POINT="${1:-$HOME/tmp/mock-scmi}"
if [[ -d $MOUNT_POINT ]]; then
	MOUNT_POINT="$(realpath "$MOUNT_POINT")"
fi
PID_FILE="${MOUNT_POINT}.mock_scmi.pid"
SCMI_INTERFACE_HINT="${MOCK_SCMI_INTERFACE:-}"
SCMI_INTERFACE_HINT="${SCMI_INTERFACE_HINT,,}"

detect_scmi_interface() {
	if [[ -e $MOUNT_POINT/tlm_0 || -L $MOUNT_POINT/tlm_0 ]]; then
		echo "ioctl"
	elif [[ -d $MOUNT_POINT/arm_telemetry/tlm-0 ]]; then
		echo "sysfs"
	else
		case "$SCMI_INTERFACE_HINT" in
		ioctl)
			echo "ioctl"
			;;
		auto | sysfs)
			echo "sysfs"
			;;
		*)
			echo "unknown"
			;;
		esac
	fi
}

SCMI_INTERFACE="$(detect_scmi_interface)"
echo "MockScmi interface: $SCMI_INTERFACE"
echo "Stopping MockScmi..."

# Prefer the PID recorded by the standalone launcher. If that file is missing,
# only fall back to MockScmi processes whose command line names this mount.
PIDS=""
if [[ -f $PID_FILE ]]; then
	while read -r PID; do
		if [[ $PID =~ ^[0-9]+$ ]] && ps -p "$PID" >/dev/null; then
			PROCESS_NAME="$(ps -p "$PID" -o comm=)"
			if [[ $PROCESS_NAME == "MockScmi" || $PROCESS_NAME == "MockScmiCuse" || $PROCESS_NAME == "sudo" ]]; then
				PIDS="${PIDS:+$PIDS }$PID"
			fi
		fi
	done <"$PID_FILE"
fi

if [[ -z $PIDS ]]; then
	CUSE_DEVICE_NAMES=""
	if [[ $SCMI_INTERFACE == "ioctl" && -d $MOUNT_POINT ]]; then
		for DEVICE_LINK in "$MOUNT_POINT"/tlm_*; do
			[[ -L $DEVICE_LINK ]] || continue
			CUSE_DEVICE_NAMES+=" $(basename "$(readlink "$DEVICE_LINK")")"
		done
	fi
	while read -r PID; do
		[[ -n $PID ]] || continue
		COMMAND_LINE="$(tr '\0' ' ' <"/proc/$PID/cmdline" 2>/dev/null || true)"
		MATCHES_CUSE_DEVICE=0
		for DEVICE_NAME in $CUSE_DEVICE_NAMES; do
			[[ $COMMAND_LINE == *"--device-name $DEVICE_NAME"* ]] && MATCHES_CUSE_DEVICE=1
		done
		if [[ $COMMAND_LINE == *"$MOUNT_POINT"* || $MATCHES_CUSE_DEVICE == 1 ]]; then
			PIDS="${PIDS:+$PIDS }$PID"
		fi
	done < <(
		pgrep -x MockScmi
		pgrep -x MockScmiCuse
		true
	)
fi

if [[ -z $PIDS ]]; then
	echo "No MockScmi processes found"
else
	echo "Found MockScmi PIDs: $PIDS"
	for PID in $PIDS; do
		echo "Stopping PID $PID..."
		kill -SIGTERM "$PID" 2>/dev/null || sudo kill -SIGTERM "$PID" 2>/dev/null || true

		# Wait up to 5 seconds for graceful shutdown
		for _ in {1..10}; do
			if ! ps -p "$PID" >/dev/null; then
				break
			fi
			sleep 0.5
		done

		# Force kill if still running
		if ps -p "$PID" >/dev/null; then
			echo "Force killing PID $PID..."
			kill -SIGKILL "$PID" 2>/dev/null || sudo kill -SIGKILL "$PID" 2>/dev/null || true
		fi
	done
	echo "MockScmi processes stopped"
fi

rm -f "$PID_FILE"

# Both sysfs and ioctl interfaces use the same FUSE mount.
if [[ $SCMI_INTERFACE != "ioctl" && -d $MOUNT_POINT ]] && mountpoint -q "$MOUNT_POINT"; then
	echo "Unmounting $MOUNT_POINT..."
	fusermount3 -u "$MOUNT_POINT" 2>/dev/null || fusermount -u "$MOUNT_POINT" 2>/dev/null || true
fi

if [[ $SCMI_INTERFACE == "ioctl" && -d $MOUNT_POINT ]]; then
	find "$MOUNT_POINT" -maxdepth 1 -type l -name 'tlm_*' -delete
fi

rmdir "$MOUNT_POINT" 2>/dev/null || true

echo "Cleanup complete"
