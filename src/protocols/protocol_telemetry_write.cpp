// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#include "protocol_telemetry_internal.hpp"

namespace mock_scmi::detail {
namespace {

auto IsEnabled(const std::string& value) -> bool { return std::atoi(value.c_str()) != 0; }

auto WriteAllDesEnable(TelemetryContext& context, const std::string& value) -> ErrorCode {
  const bool enabled = IsEnabled(value);
  if (context.is_group) {
    for (const auto& event : context.target.GetDataEvents()) {
      if (event->group_id_ == context.group_id) {
        event->enable_ = enabled;
      }
    }
  } else {
    context.target.SetAllDesEnableFlag(enabled);
    std::cout << "Set all_des_enable to " << (enabled ? "2" : "0") << '\n';
    for (const auto& event : context.target.GetDataEvents()) {
      event->enable_ = enabled;
    }
  }
  return ErrorCode::SUCCESS;
}

auto WriteAllDesTstampEnable(TelemetryContext& context, const std::string& value) -> ErrorCode {
  const bool enabled = IsEnabled(value);
  context.target.SetAllDesTstampEnableFlag(enabled);
  std::cout << "Set all_des_tstamp_enable to " << (enabled ? "1" : "0") << '\n';
  for (const auto& event : context.target.GetDataEvents()) {
    event->tstamp_enable_ = enabled;
  }
  return ErrorCode::SUCCESS;
}

auto ParseUpdateInterval(const std::string& value) -> std::optional<update_interval_t> {
  int               seconds  = 0;
  int16_t           exponent = 0;
  std::stringstream parser(value);
  if (!(parser >> seconds >> exponent)) {
    return std::nullopt;
  }
  return update_interval_t{std::chrono::seconds{seconds}, exponent};
}

auto SupportsInterval(const UpdateInterval& intervals, const update_interval_t& target) -> bool {
  return !intervals.discrete || std::find(intervals.update_intervals.begin(), intervals.update_intervals.end(),
                                          target) != intervals.update_intervals.end();
}

auto WriteCurrentUpdateInterval(TelemetryContext& context, const std::string& value) -> ErrorCode {
  const auto target_interval = ParseUpdateInterval(value);
  if (!target_interval.has_value()) {
    std::cerr << "Unsupported update interval: " << value << '\n';
    return ErrorCode::UNSUPPORTED_PROTOCOL;
  }

  const auto [seconds, exponent] = *target_interval;
  if (context.is_group) {
    const auto& group = context.target.GetGroups().at(context.group_id.value());
    if (!SupportsInterval(group->intervals_, *target_interval)) {
      std::cerr << "Unsupported update interval: " << seconds.count() << "," << exponent << '\n';
      return ErrorCode::UNSUPPORTED_PROTOCOL;
    }

    // TODO(danngu01): validate non-discrete intervals against the configured range and step size.
    group->intervals_.active_update_interval = *target_interval;
    std::cout << "Set group current_update_interval_ms to " << seconds.count() << "," << exponent << '\n';
    return ErrorCode::SUCCESS;
  }

  UpdateInterval intervals{
      .discrete         = context.target.GetIntervalsAreDiscreteFlag(),
      .update_intervals = context.target.GetAvailableUpdateIntervals(),
  };
  if (!SupportsInterval(intervals, *target_interval)) {
    std::cerr << "Unsupported update interval: " << seconds.count() << "," << exponent << '\n';
    return ErrorCode::UNSUPPORTED_PROTOCOL;
  }

  context.target.SetCurrentUpdateInterval(*target_interval);
  std::cout << "Set current_update_interval_ms to " << seconds.count() << "," << exponent << '\n';
  return ErrorCode::SUCCESS;
}

auto WriteTlmEnable(TelemetryContext& context, const std::string& value) -> ErrorCode {
  const bool enabled = IsEnabled(value);
  context.target.SetTlmEnableFlag(enabled);
  std::cout << "Set tlm_enable to " << (enabled ? "1" : "0") << '\n';
  return ErrorCode::SUCCESS;
}

auto WriteEventEnable(TelemetryContext& context, const std::string& value) -> ErrorCode {
  const bool enabled = IsEnabled(value);
  if (context.is_group) {
    for (const auto& event : context.target.GetDataEvents()) {
      if (event->group_id_ == context.group_id) {
        event->enable_ = enabled;
      }
    }
    return ErrorCode::SUCCESS;
  }

  if (context.parent == nullptr) {
    std::cerr << "Can't enable node with no parent node\n";
    return ErrorCode::MOUNTPOINT_MISSING;
  }

  if (auto* event = context.target.GetDataEventById(ParentDataEventId(context))) {
    event->enable_ = enabled;
  }
  return ErrorCode::SUCCESS;
}

auto WriteEventTstampEnable(TelemetryContext& context, const std::string& value) -> ErrorCode {
  const bool enabled = IsEnabled(value);
  if (context.is_group) {
    for (const auto& event : context.target.GetDataEvents()) {
      if (event->group_id_ == context.group_id) {
        event->tstamp_enable_ = enabled;
      }
    }
    return ErrorCode::SUCCESS;
  }

  const auto data_event_id = ParentDataEventId(context);
  std::cout << "id is: " << data_event_id << '\n';
  context.target.GetDataEventById(data_event_id)->tstamp_enable_ = enabled;
  return ErrorCode::SUCCESS;
}

}  // namespace

auto HandleTelemetryWrite(const FileSystemNode* node, const std::string& value) -> ErrorCode {
  auto      context = BuildTelemetryContext(node);
  ErrorCode result{ErrorCode::UNSUPPORTED_PROTOCOL};

  switch (context.file_type) {
    case TelemetryFile::ALL_DES_ENABLE:
      result = WriteAllDesEnable(context, value);
      break;
    case TelemetryFile::ALL_DES_TSTAMP_ENABLE:
      result = WriteAllDesTstampEnable(context, value);
      break;
    case TelemetryFile::CURRENT_UPDATE_INTERVAL_MS:
      result = WriteCurrentUpdateInterval(context, value);
      break;
    case TelemetryFile::TLM_ENABLE:
      result = WriteTlmEnable(context, value);
      break;
    case TelemetryFile::ENABLE:
      result = WriteEventEnable(context, value);
      break;
    case TelemetryFile::TSTAMP_ENABLE:
      result = WriteEventTstampEnable(context, value);
      break;
    default:
      std::cout << "Unknown telemetry file: " << context.file_name << '\n';
      break;
  }

  return result;
}

}  // namespace mock_scmi::detail
