#!/bin/env bash

# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail

MOUNT_POINT="${PWD}"
(
	cd "${TEST_SCRATCH}"
	"$1" "${MOUNT_POINT}/tlm_0"
)
find . -maxdepth 1 \( -type f -o -type l \) | sort -u
