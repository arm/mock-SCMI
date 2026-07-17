// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <ranges>
#include <string>

#include "protocol_telemetry_internal.hpp"

namespace mock_scmi::detail {
namespace {

auto FormatInterval(const update_interval_t& interval) -> std::string {
  return std::format("{},{}\n", ToRawString<std::chrono::seconds>(interval.first), interval.second);
}

auto FormatIntervalRange(const UpdateInterval& intervals) -> std::string {
  if (intervals.update_intervals.empty()) {
    return "";
  }

  const auto min_interval = GetInterval(intervals.update_intervals, std::ranges::min_element);
  const auto max_interval = GetInterval(intervals.update_intervals, std::ranges::max_element);
  return std::format("{},{} {},{} {},{}\n", ToRawString<std::chrono::seconds>(min_interval.first), min_interval.second,
                     ToRawString<std::chrono::seconds>(max_interval.first), max_interval.second,
                     ToRawString<std::chrono::seconds>(intervals.step_size.first), intervals.step_size.second);
}

auto ActiveInterval(const TelemetryContext& context) -> update_interval_t {
  if (context.is_group) {
    return context.target.GetGroups().at(context.group_id.value())->intervals_.active_update_interval;
  }
  return context.target.GetCurrentUpdateInterval();
}

auto ToMilliseconds(const update_interval_t& interval) -> std::chrono::milliseconds {
  const auto [base, exponent] = interval;
  const auto seconds =
      ToRaw<std::chrono::seconds>(base) * std::pow(10.0, exponent);  // NOLINT(cppcoreguidelines-narrowing-conversions)
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::duration<double>(seconds));
}

auto IsEventReadable(const TelemetryContext& context, const DataEvent& event) -> bool {
  if (!event.enable_ || !context.target.GetTlmEnableFlag()) {
    return false;
  }
  return !context.is_group || event.group_id_ == context.group_id;
}

auto FormatEventSample(const DataEvent& event) -> std::string {
  std::string result = event.tstamp_enable_ ? std::to_string(event.GetScaledTimestamp()) + ": " : "0: ";
  result += std::format("0x{:04x} ", static_cast<unsigned int>(event.id_));
  // TODO(ASTL-116): Handle all other astl_value_type_t values.
  result += std::format("{:016x} \n", event.last_value_.ui64);
  return result;
}

auto ReadCurrentUpdateInterval(const TelemetryContext& context) -> std::string {
  return FormatInterval(ActiveInterval(context));
}

auto ReadBulkSamples(const TelemetryContext& context) -> std::string {
  const auto now      = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
  const auto interval = ToMilliseconds(ActiveInterval(context));

  std::string result;
  for (const auto& event : context.target.GetDataEvents()) {
    if (event && IsEventReadable(context, *event)) {
      UpdateEventByInterval(event.get(), now, interval);
      result += FormatEventSample(*event);
    }
  }
  return result;
}

auto ReadIntervalsDiscrete(const TelemetryContext& context) -> std::string {
  if (context.is_group) {
    return FormatIntervalRange(context.target.GetGroups().at(context.group_id.value())->intervals_);
  }
  if (context.target.GetIntervalsAreDiscreteFlag()) {
    return "0\n";
  }

  return FormatIntervalRange(UpdateInterval{
      .discrete         = false,
      .update_intervals = context.target.GetAvailableUpdateIntervals(),
      .step_size        = context.target.GetStepSize(),
  });
}

auto ReadEventEnable(const TelemetryContext& context) -> std::string {
  if (context.is_group) {
    return context.target.GetGroups().at(context.group_id.value())->enable_ ? "1\n" : "0\n";
  }
  if (context.parent == nullptr) {
    return "";
  }
  if (const auto* event = context.target.GetDataEventById(ParentDataEventId(context))) {
    return event->enable_ ? "1\n" : "0\n";
  }
  return "";
}

auto ReadEventTstampEnable(const TelemetryContext& context) -> std::string {
  if (context.is_group) {
    return context.target.GetGroups().at(context.group_id.value())->tstamp_enable_ ? "1\n" : "0\n";
  }
  const auto* event = context.target.GetDataEventById(ParentDataEventId(context));
  return std::to_string(static_cast<int>(event->tstamp_enable_)) + "\n";
}

auto ReadEventTstampRate(const TelemetryContext& context) -> std::string {
  const auto* event = context.target.GetDataEventById(ParentDataEventId(context));
  return std::to_string(event->tstamp_rate_) + "\n";
}

auto ReadEventValue(const TelemetryContext& context) -> std::string {
  auto* event = context.target.GetDataEventById(ParentDataEventId(context));
  if (!event->enable_ || !context.target.GetTlmEnableFlag()) {
    return "0\n";
  }

  const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
  UpdateEventByInterval(event, now, ToMilliseconds(context.target.GetCurrentUpdateInterval()));

  // TODO(ASTL-116): Handle all other astl_value_type_t values.
  const auto timestamp = event->tstamp_enable_ ? event->GetScaledTimestamp() : 0;
  return std::format("{} {:016x}\n", timestamp, event->last_value_.ui64);
}

auto ReadSingleSamples(const TelemetryContext& context) -> std::string {
  std::string result;
  for (const auto& event : context.target.GetDataEvents()) {
    if (event && IsEventReadable(context, *event)) {
      event->last_value_ = event->Generate();
      result += FormatEventSample(*event);
    }
  }
  return result;
}

}  // namespace

auto HandleTelemetryRead(const FileSystemNode* node) -> std::string {
  const auto  context = BuildTelemetryContext(node);
  std::string result;

  switch (context.file_type) {
    case TelemetryFile::CURRENT_UPDATE_INTERVAL_MS:
      result = ReadCurrentUpdateInterval(context);
      break;
    case TelemetryFile::DES_BULK_READ:
      result = ReadBulkSamples(context);
      break;
    case TelemetryFile::INTERVALS_DISCRETE:
      result = ReadIntervalsDiscrete(context);
      break;
    case TelemetryFile::TLM_ENABLE:
      result = context.target.GetTlmEnableFlag() ? "1\n" : "0\n";
      break;
    case TelemetryFile::ENABLE:
      result = ReadEventEnable(context);
      break;
    case TelemetryFile::TSTAMP_ENABLE:
      result = ReadEventTstampEnable(context);
      break;
    case TelemetryFile::TSTAMP_RATE:
      result = ReadEventTstampRate(context);
      break;
    case TelemetryFile::VALUE:
      result = ReadEventValue(context);
      break;
    case TelemetryFile::DES_SINGLE_SAMPLE_READ:
      result = ReadSingleSamples(context);
      break;
    default:
      std::cout << "Unknown telemetry file: " << context.file_name << '\n';
      break;
  }

  return result;
}

}  // namespace mock_scmi::detail
