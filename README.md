<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
SPDX-License-Identifier: Apache-2.0
-->

# mock_scmi

`mock_scmi` provides a userspace mock of the Arm SCMI telemetry interface. It
can expose either the SCMI sysfs-style tree through FUSE or mock telemetry
devices for ioctl clients.

## Prerequisites

The build requires:

- Linux with a C++20 compiler
- CMake 3.14 or newer
- pkg-config
- libfuse 3.12 or newer, including its development headers
- the Linux CUSE module for the ioctl backend

On Ubuntu or Debian, install the system dependencies with:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake pkg-config libfuse3-dev fuse3
```

The header-only nlohmann/json dependency is downloaded automatically by CMake
at its pinned version. To build without network access, install
`nlohmann-json3-dev` first or set CMake's
`FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON` variable to an existing source tree.
Unlike ASTL, this standalone build does not require vcpkg: libfuse is a system
dependency so that it matches the host's FUSE/CUSE runtime, while CMake manages
the pinned, header-only nlohmann/json dependency.

## Build and test

The shortest workflow uses [`just`](https://github.com/casey/just):

```sh
just build
just test sysfs
just test ioctl
just launch sysfs
just launch ioctl
just cleanup
```

The launch commands mount at `~/tmp/mock-scmi` by default and leave MockScmi
running in the background. Pass another mount point as the second argument,
for example `just launch sysfs ~/tmp/customer-scmi`. The launcher creates the
mount-point directory when needed. Set `MOCK_SCMI_MOUNT_POINT` to change the
default, or set `BUILD_TYPE` or `BUILD_DIR` to override their `Release` and
`build` defaults. The two test commands select the sysfs or ioctl integration
suite respectively; both also run the portable command-line smoke test.
`just cleanup` detects which backend is mounted, stops the instance recorded by
the launcher, unmounts it, and removes its mount artifacts. Pass the same custom
mount point to cleanup when one was used: `just cleanup ~/tmp/customer-scmi`.

The equivalent CMake commands are:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Formatting and linting

This repository uses the same formatting and linting policies as ASTL:
clang-format, clang-tidy, cppcheck, cmakelang, ShellCheck/shfmt, YAML and
Markdown linting, GitHub Actions validation, security scanning through Qlty,
and REUSE license validation.

```sh
just format       # Apply formatting
just check-format # Check formatting without modifying files
just lint         # Lint changes relative to main
just lint-all     # Lint the complete repository
just quality      # Run every non-mutating quality check
```

Install `pre-commit`, clang-format, clang-tidy, cppcheck, cmakelang, ShellCheck,
REUSE 3.3 or newer, jq, and the Qlty CLI, then enable the local hooks with:

```sh
pre-commit install
```

The hooks format and re-stage changed files, then run Qlty, license validation,
cppcheck, and CMake linting as applicable. The same core checks run in
`.github/workflows/lint.yml` for pushes to `main` and pull requests.

The basic command-line smoke test runs on every Linux system. Integration tests
need read/write access to `/dev/fuse`; CTest reports them as skipped when FUSE
is unavailable (for example, in an unprivileged container). If Valgrind is
installed, the integration tests also perform leak checking automatically.
The ioctl integration test uses CUSE because the SCMI batch ioctls contain
nested userspace pointers. Load it with `sudo modprobe cuse`; if `/dev/cuse` is
root-only, run `sudo -v` before `just test ioctl`. `just launch ioctl` requests
sudo authorization for CUSE device creation and leaves the build unprivileged.

To install the executable and sample configurations:

```sh
cmake --install build --prefix ./install
```

## Run

Create a mount point, then keep the mock in the foreground:

```sh
mkdir -p ~/tmp/mock-scmi
./build/MockScmi -f -s --tlm-json-path ./config/tlm.json ~/tmp/mock-scmi
```

Use another terminal to inspect or modify the generated tree. Unmount it with:

```sh
fusermount3 -u ~/tmp/mock-scmi
```

The direct `MockScmi` invocation above is the FUSE/sysfs server. Use
`just launch ioctl` for the ioctl backend; it starts unrestricted CUSE
character devices and exposes them as `tlm_N` entries below the selected root.
`MOCK_SCMI_INTERFACE` selects only the backend exposed by this server. Clients
choose their transport independently: automatic discovery needs no matching
client setting, while a client that forces a transport must be configured for
the same `sysfs` or `ioctl` mode.
