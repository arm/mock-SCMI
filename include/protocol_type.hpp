// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_PROTOCOL_TYPE_HPP
#define INCLUDE_PROTOCOL_TYPE_HPP

namespace mock_scmi {

/**
 * @brief Specifies the SCMI protocol type associated with a file system node.
 *
 * @details This enum defines the supported SCMI protocols. Currently, it includes:
 * - @c NONE: No protocol.
 * - @c SCMI_SYSFS_TELEMETRY: The SCMI SYSFS telemetry protocol.
 * - @c SCMI_IOCTL_TELEMETRY: The SCMI IOCTL telemetry protocol.
 *
 * The enum is designed to be extensible for future protocols, such as additional SCMI drivers
 * for power management, sensor management, etc.
 */
enum class ProtocolType {
  NONE,
  SCMI_SYSFS_TELEMETRY,
  SCMI_IOCTL_TELEMETRY,
  // Add additional protocols here...
};

}  // namespace mock_scmi

#endif  // INCLUDE_PROTOCOL_TYPE_HPP
