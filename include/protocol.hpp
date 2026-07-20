// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_PROTOCOL_HPP
#define INCLUDE_PROTOCOL_HPP

#include <cstddef>
#include <optional>
#include <string>

#include "common.hpp"
#include "fsnode.hpp"

namespace mock_scmi {

struct IoctlReply;

/**
 * @brief Initializes SCMI protocol handlers for the file system.
 *
 * @details Currently initializes the telemetry protocol handler by creating its
 * subtree under the provided root node. This function is extensible to initialize
 * additional protocols.
 *
 * @param g_root Pointer to the root file system node.
 * @param config_file_path Optional protocol configuration file path from the command line.
 */
auto InitProtocol(FileSystemNode* g_root, const std::optional<std::string>& config_file_path) -> void;

/**
 * @brief Initializes SCMI ioctl protocol handlers for the file system.
 *
 * @details Creates a device-like file per configured telemetry target at the
 * mount root, using the ioctl naming convention (`tlm_0`) rather than the
 * legacy sysfs directory convention (`tlm-0`).
 *
 * @param g_root Pointer to the root file system node.
 * @param config_file_path Optional protocol configuration file path from the command line.
 */
auto InitIoctlProtocol(FileSystemNode* g_root, const std::optional<std::string>& config_file_path) -> void;

/**
 * @brief Dispatches the write operation for a file system node based on its protocol.
 *
 * @details This function examines the protocol type of the provided file system node and delegates
 * the write operation to the corresponding protocol-specific handler. For example, if the node's protocol
 * is SCMI telemetry, it calls the telemetry write handler (e.g. @ref HandleProtocolTelemetryWrite).
 * Additional protocol cases can be added within the switch statement. If no handler exists for the node's
 * protocol, an appropriate error is logged and ErrorCode::UNSUPPORTED_PROTOCOL is returned.
 *
 * @param node A pointer to the file system node for which the write operation is to be handled.
 * @param value The string value to be written.
 * @return ErrorCode Status code indicating success (e.g. ErrorCode::SUCCESS) or the type of error encountered.
 */
auto HandleProtocolWrite(const FileSystemNode* node, const std::string& value) -> ErrorCode;

/**
 * @brief Dispatches the read operation for a file system node based on its protocol.
 *
 * @details This function inspects the protocol associated with the given file system node and delegates
 * the read operation to the corresponding protocol-specific handler. For example, if the node's protocol
 * is SCMI telemetry, it calls the telemetry read handler (e.g. @ref HandleProtocolTelemetryRead).
 * Additional protocol cases can be supported by extending the switch statement. If no handler exists for the
 * node's protocol, an empty string is returned.
 *
 * @param node A pointer to the file system node to be read.
 * @return std::string The data read from the node as a string, or an empty string if the protocol is unsupported.
 */
auto HandleProtocolRead(const FileSystemNode* node) -> std::string;

/**
 * @brief Dispatches an ioctl operation for a file system node based on its protocol.
 *
 * @param node A pointer to the file system node receiving the ioctl.
 * @param cmd Encoded ioctl command.
 * @param in_buf Input buffer provided by FUSE for commands with write direction.
 * @param in_bufsz Size of in_buf in bytes.
 * @return IoctlReply containing either errno-compatible error_code or output data.
 */
auto HandleProtocolIoctl(const FileSystemNode* node, unsigned int cmd, const void* in_buf, size_t in_bufsz)
    -> IoctlReply;

}  // namespace mock_scmi

#endif  // INCLUDE_PROTOCOL_HPP
