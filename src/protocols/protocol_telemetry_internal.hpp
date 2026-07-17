// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MOCK_SCMI_PROTOCOL_TELEMETRY_INTERNAL_HPP
#define MOCK_SCMI_PROTOCOL_TELEMETRY_INTERNAL_HPP

#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "protocol_telemetry.hpp"

namespace mock_scmi {

constexpr long double kUpdateIntervalExponentBase{10.0L};

auto GetTelemetryFile(const std::string& name) -> TelemetryFile;

template <typename ToDuration = std::chrono::milliseconds, typename Rep, typename Period>
inline uint64_t ToRaw(const std::chrono::duration<Rep, Period>& duration) {
  return std::chrono::duration_cast<ToDuration>(duration).count();
}

template <typename ToDuration = std::chrono::milliseconds, typename Clock, typename Duration>
inline uint64_t ToRaw(const std::chrono::time_point<Clock, Duration>& timepoint) {
  return std::chrono::duration_cast<ToDuration>(timepoint.time_since_epoch()).count();
}

template <typename ToDuration = std::chrono::milliseconds, typename T>
inline std::string ToRawString(const T& value) {
  return std::to_string(ToRaw<ToDuration>(value));
}

inline auto UpdateEventByInterval(DataEvent* event, std::chrono::system_clock::time_point now,
                                  std::chrono::milliseconds interval) -> void {
  auto elapsed = now - event->last_timestamp_;
  if (elapsed < interval) {
    return;
  }

  auto ticks = ToRaw(elapsed) / ToRaw(interval);
  for (uint64_t i = 0; i < ticks; ++i) {
    event->last_value_ = event->Generate();
  }
}

template <class Selector>
update_interval_t GetInterval(const std::vector<update_interval_t>& intervals, Selector selector) {
  auto comp = [](const auto& lhs, const auto& rhs) {
    long double lhs_value = ToRaw<std::chrono::seconds>(lhs.first) * std::pow(kUpdateIntervalExponentBase, lhs.second);
    long double rhs_value = ToRaw<std::chrono::seconds>(rhs.first) * std::pow(kUpdateIntervalExponentBase, rhs.second);
    return lhs_value < rhs_value;
  };
  return *selector(intervals, comp);
}

namespace detail {

struct TelemetryContext {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-const-or-ref-data-members): non-owning target that must not be null
  SCMITelemetryTarget&      target;
  std::string               file_name;
  TelemetryFile             file_type;
  const FileSystemNode*     parent;
  bool                      is_group;
  std::optional<group_id_t> group_id;
};

inline auto BuildTelemetryContext(const FileSystemNode* node) -> TelemetryContext {
  const FileSystemNode* target_node = node;
  while (target_node->GetParent() && target_node->GetParent()->GetName() != "arm_telemetry") {
    target_node = target_node->GetParent();
  }

  const FileSystemNode*     parent       = node->GetParent();
  const FileSystemNode*     grand_parent = parent ? parent->GetParent() : nullptr;
  const bool                is_group     = grand_parent != nullptr && grand_parent->GetName() == "groups";
  std::optional<group_id_t> group_id;
  if (is_group) {
    group_id = static_cast<group_id_t>(std::stoul(parent->GetName()));
  }

  std::string file_name = node->GetName();
  return TelemetryContext{
      .target    = SCMITelemetryTarget::Instance(target_node->GetName()),
      .file_name = file_name,
      .file_type = GetTelemetryFile(file_name),
      .parent    = parent,
      .is_group  = is_group,
      .group_id  = group_id,
  };
}

inline auto ParentDataEventId(const TelemetryContext& context) -> data_event_id_t {
  return static_cast<data_event_id_t>(std::stoul(context.parent->GetName(), nullptr, kHexRadix));
}

auto HandleTelemetryWrite(const FileSystemNode* node, const std::string& value) -> ErrorCode;
auto HandleTelemetryRead(const FileSystemNode* node) -> std::string;

}  // namespace detail
}  // namespace mock_scmi

#endif  // MOCK_SCMI_PROTOCOL_TELEMETRY_INTERNAL_HPP
