// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_PROTOCOL_SCMI_IOCTL_HPP
#define INCLUDE_PROTOCOL_SCMI_IOCTL_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "fsnode.hpp"

namespace mock_scmi {

struct IoctlReply {
  int               error_code{0};
  std::vector<char> output;
};

auto InitProtocolScmiIoctl(FileSystemNode* g_root, const std::optional<std::string>& config_file_path) -> void;

auto HandleProtocolScmiIoctl(const FileSystemNode* node, unsigned int cmd, const void* in_buf, size_t in_bufsz)
    -> IoctlReply;

}  // namespace mock_scmi

#endif  // INCLUDE_PROTOCOL_SCMI_IOCTL_HPP
