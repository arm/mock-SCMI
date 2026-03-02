#!/usr/bin/env bash

# SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

set -eu -o pipefail

START_DIR=$(pwd)
echo '--- Directory List ---'
find . -type d | sort | tee "${TEST_SCRATCH}/directory_list.txt"

for CUR_DIR in $(cat "${TEST_SCRATCH}/directory_list.txt"); do
	cd "${START_DIR}"
	echo "--- ${CUR_DIR} ---"
	cd "${CUR_DIR}"
	ls | sort
done
