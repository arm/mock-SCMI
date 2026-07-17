// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_MOCK_SCMI_HPP_
#define INCLUDE_MOCK_SCMI_HPP_

#include <fuse_lowlevel.h>

#include <memory>
#include <optional>
#include <string>

#include "fsnode.hpp"
#include "scmi_backend_selection.hpp"

namespace mock_scmi {

constexpr size_t kMaxContentLen = 1024;

constexpr mode_t kModeReadExecute = 0555;  // Read and execute
constexpr mode_t kModeReadOnly    = 0444;  // Read only
constexpr mode_t kModeReadWrite   = 0666;  // Read and write
constexpr mode_t kModeWriteOnly   = 0222;  // Write only

constexpr int    kDirectoryLinkCount = 2;     /// Directories always have at least two links ('.' and '..').
constexpr int    kFileLinkCount      = 1;     /// Files typically have one link.
constexpr off_t  kDefaultDirSize     = 4096;  /// Default size for a directory in bytes.
constexpr double kAttrTimeoutSec     = 1.0;   /// Timeout for file attribute caching (in seconds).
constexpr double kEntryTimeoutSec    = 1.0;   /// Timeout for directory entry caching (in seconds).

/**
 * @brief Main entry point for a FUSE-based file system.
 *
 * @details This program uses libfuse, a library that enables the implementation
 * of file systems in user space (FUSE). It initializes a FUSE session using the
 * provided command-line arguments, sets up the file system, and enters the FUSE
 * event loop. Simple options for help, version information, and single/multi-threaded
 * operation are supported.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 *
 * @return int Exit status of the program.
 */
extern const struct fuse_lowlevel_ops kFuseLowLevelOps;

struct FuseUserData {
  std::unique_ptr<FileSystemNode> root;
  std::optional<std::string>      tlm_json_path;
  ScmiBackendPreference           scmi_backend_preference{ScmiBackendPreference::AUTO};
};

}  // namespace mock_scmi

#endif  // INCLUDE_MOCK_SCMI_HPP_
