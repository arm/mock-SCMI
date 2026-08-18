// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_COMMON_HPP_
#define INCLUDE_COMMON_HPP_

#include <cstdint>
#include <string>

namespace mock_scmi {

enum class ErrorCode {
  SUCCESS                 = 0,
  CMDLINE_PARSE_ERROR     = 1,
  MOUNTPOINT_MISSING      = 2,
  SESSION_CREATION_FAILED = 3,
  SIGNAL_HANDLER_ERROR    = 4,
  SYSFS_MOUNT_FAILED      = 5,
  SESSION_LOOP_FAILED     = 6,

  UNSUPPORTED_PROTOCOL = 7,
};

/***********************************************************************************
 **********************               DATA TYPES               *********************
 **********************************************************************************/

/** Mock-local value types parsed from the telemetry JSON configuration.
 *
 * These values are not serialized as an ASTL API enum. The sysfs interface
 * emits textual samples, while the ioctl interface publishes the value size
 * defined by the SCMI telemetry UAPI. Consequently, this enum and its value
 * container do not need to track ASTL's internal value-type declarations.
 */
typedef enum _mock_scmi_value_type_t {
  MOCK_SCMI_VALUE_UINT8   = 0,  //!< Unsigned 8bit integer (char)
  MOCK_SCMI_VALUE_UINT16  = 1,  //!< Unsigned 16bit integer (short)
  MOCK_SCMI_VALUE_UINT32  = 2,  //!< Unsigned 32bit integer
  MOCK_SCMI_VALUE_UINT64  = 3,  //!< Unsigned 64bit integer (long)
  MOCK_SCMI_VALUE_FLOAT32 = 4,  //!< 32bit float
  MOCK_SCMI_VALUE_FLOAT64 = 5,  //!< 64bit float (double)
  MOCK_SCMI_VALUE_BOOL8   = 6,  //!< 8bit boolean

  MOCK_SCMI_VALUE_UNKNOWN = -1,  //!< Unknown
} mock_scmi_value_type_t;

/** Value container. Processing of a mock_scmi_value_t should be based on mock_scmi_value_type_t
 * All readings will use this 64bit union to capture any data 64bit in size or less.
 * If there is a reading that is more than 64bit, more than one counter would be used
 * to capture all reading in up to 64bit chunks
 */
typedef union _mock_scmi_value_t {
  uint8_t  ui8;   //!< 8bits unsigned integer for UINT8
  uint16_t ui16;  //!< 16bits unsigned integer for UINT16
  uint32_t ui32;  //!< 32bits unsigned integer for UINT32
  uint64_t ui64;  //!< 64bits unsigned integer for UINT64
  float    fp32;  //!< 32bits float for FLOAT32
  double   fp64;  //!< 64bits float for FLAAT64
  bool     b8;    //!< 8bits boolean for BOOL8
} mock_scmi_value_t;

inline mock_scmi_value_type_t ToValueType(const std::string& str) {
  mock_scmi_value_type_t value_type = MOCK_SCMI_VALUE_UNKNOWN;
  if (str == "UINT8") {
    value_type = MOCK_SCMI_VALUE_UINT8;
  } else if (str == "UINT16") {
    value_type = MOCK_SCMI_VALUE_UINT16;
  } else if (str == "UINT32") {
    value_type = MOCK_SCMI_VALUE_UINT32;
  } else if (str == "UINT64") {
    value_type = MOCK_SCMI_VALUE_UINT64;
  } else if (str == "FLOAT32") {
    value_type = MOCK_SCMI_VALUE_FLOAT32;
  } else if (str == "FLOAT64") {
    value_type = MOCK_SCMI_VALUE_FLOAT64;
  } else if (str == "BOOL8") {
    value_type = MOCK_SCMI_VALUE_BOOL8;
  }
  return value_type;
}

using group_id_t      = uint32_t;
using data_event_id_t = uint32_t;

}  // namespace mock_scmi

#endif  // INCLUDE_COMMON_HPP_
