# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

set shell := ["bash", "-eu", "-o", "pipefail", "-c"]

root := justfile_directory()
build_dir := env_var_or_default("BUILD_DIR", "build")
build_type := env_var_or_default("BUILD_TYPE", "Release")
default_mount_point := env_var_or_default("MOCK_SCMI_MOUNT_POINT", env_var("HOME") + "/tmp/mock-scmi")

# Configure and compile MockScmi.
build:
    cmake -S "{{root}}" -B "{{build_dir}}" -DCMAKE_BUILD_TYPE="{{build_type}}"
    cmake --build "{{build_dir}}" --parallel

# Build and test one SCMI backend: `just test sysfs` or `just test ioctl`.
test mode: build
    #!/usr/bin/env bash
    case "{{mode}}" in
        sysfs|ioctl) ;;
        *) echo "Usage: just test [sysfs|ioctl]" >&2; exit 2 ;;
    esac
    ctest --test-dir "{{build_dir}}" --output-on-failure -L "smoke|backend_{{mode}}"

# Build and launch one SCMI backend, optionally at a custom mount point.
launch mode mount_point=default_mount_point: build
    #!/usr/bin/env bash
    case "{{mode}}" in
        sysfs|ioctl) ;;
        *) echo "Usage: just launch [sysfs|ioctl] [mount-point]" >&2; exit 2 ;;
    esac
    MOCK_SCMI_INTERFACE="{{mode}}" \
        MOCK_SCMI_BIN="$(realpath "{{build_dir}}/MockScmi")" \
        MOCK_SCMI_CUSE_BIN="$(realpath "{{build_dir}}/MockScmiCuse")" \
        "{{root}}/scripts/launch_mockscmi.sh" "{{mount_point}}"

# Stop the MockScmi instance at a mount point and unmount its FUSE tree.
cleanup mount_point=default_mount_point:
    "{{root}}/scripts/cleanup_mockscmi.sh" "{{mount_point}}"

# Format source, CMake, shell, Markdown, and YAML files with the ASTL rules.
format:
    "{{root}}/scripts/format.sh"

# Check clang-format and cmake-format without changing files.
check-format:
    "{{root}}/scripts/check_format.sh"

# Lint files changed from main with clang-tidy and ShellCheck.
lint: build
    "{{root}}/scripts/lint.sh" "{{build_dir}}" pull-request

# Lint staged C/C++ and shell files.
lint-staged: build
    "{{root}}/scripts/lint.sh" "{{build_dir}}" pre-commit

# Lint every C/C++ and shell file.
lint-all: build
    "{{root}}/scripts/lint.sh" "{{build_dir}}" all

# Run cppcheck with the CMake compilation database.
cppcheck: build
    "{{root}}/scripts/cppcheck.sh" "{{build_dir}}"

# Lint CMake files with cmakelang.
cmake-lint:
    "{{root}}/scripts/cmakelang.sh"

# Validate SPDX and REUSE licensing metadata.
license-lint:
    "{{root}}/scripts/license_lint.sh"

# Run Qlty's ASTL plugin set without modifying files.
qlty:
    qlty check --all --no-fix

# Run all non-mutating formatting, lint, static-analysis, and license checks.
quality: check-format lint-all cppcheck cmake-lint license-lint qlty
