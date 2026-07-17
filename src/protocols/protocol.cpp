// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "protocol.hpp"

#include <cerrno>
#include <iostream>

#include "common.hpp"
#include "fsnode.hpp"
#include "protocol_scmi_ioctl.hpp"
#include "protocol_telemetry.hpp"  // Contains InitProtocolTelemetry() and HandleProtocolTelemetry()

namespace mock_scmi {

auto InitProtocol(FileSystemNode* g_root, const std::optional<std::string>& config_file_path) -> void {
  // Initialize additional protocols here
  std::unique_ptr<FileSystemNode> telemetry_tree = InitProtocolTelemetry(g_root, config_file_path);
  g_root->AddChild(std::move(telemetry_tree));
}

auto InitIoctlProtocol(FileSystemNode* g_root, const std::optional<std::string>& config_file_path) -> void {
  InitProtocolScmiIoctl(g_root, config_file_path);
}

auto HandleProtocolWrite(const FileSystemNode* node, const std::string& value) -> ErrorCode {
  switch (node->GetProtocol()) {
    case ProtocolType::SCMI_SYSFS_TELEMETRY:
      return HandleProtocolTelemetryWrite(node, value);

      // Add additional cases for other protocols as needed:
      // case ProtocolType::ANOTHER_PROTOCOL:
      //     return HandleAnotherProtocolWrite(node, value);

    default:
      std::cerr << "No write handler for protocol." << '\n';
      return ErrorCode::UNSUPPORTED_PROTOCOL;
  }
}

auto HandleProtocolRead(const FileSystemNode* node) -> std::string {
  switch (node->GetProtocol()) {
    case ProtocolType::SCMI_SYSFS_TELEMETRY:
      return HandleProtocolTelemetryRead(node);

      // Add additional cases for other protocols as needed:
      // case ProtocolType::ANOTHER_PROTOCOL:
      //     return HandleAnotherProtocolRead(node);

    default:
      std::cerr << "No read handler for protocol." << '\n';
      return "";
  }
}

auto HandleProtocolIoctl(const FileSystemNode* node, unsigned int cmd, const void* in_buf, size_t in_bufsz)
    -> IoctlReply {
  switch (node->GetProtocol()) {
    case ProtocolType::SCMI_IOCTL_TELEMETRY:
      return HandleProtocolScmiIoctl(node, cmd, in_buf, in_bufsz);

    default:
      std::cerr << "No ioctl handler for protocol." << '\n';
      return {.error_code = ENOTTY};
  }
}

}  // namespace mock_scmi
