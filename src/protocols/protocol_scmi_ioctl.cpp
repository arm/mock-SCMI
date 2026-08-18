// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "protocol_scmi_ioctl.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "fsnode.hpp"
#include "protocol_telemetry.hpp"
#include "protocol_type.hpp"
#include "scmi_ioctl_uapi.hpp"

namespace mock_scmi {
namespace {

constexpr uint32_t kDefaultDataSize  = 8;
constexpr uint32_t kUint64DataSize   = 8;
constexpr size_t   kHexDigitsPerByte = 2;

template <typename T>
auto ReplyWith(const T& value) -> IoctlReply {
  IoctlReply reply{};
  reply.output.resize(sizeof(T));
  std::memcpy(reply.output.data(), &value, sizeof(T));
  return reply;
}

template <typename T>
auto ReadInput(const void* in_buf, size_t in_bufsz, T& value) -> bool {
  const bool valid = in_buf != nullptr && in_bufsz >= sizeof(T);
  if (valid) {
    std::memcpy(&value, in_buf, sizeof(T));
  }
  return valid;
}

auto ToDeviceName(std::string target_id) -> std::string {
  if (target_id.starts_with("tlm-")) {
    target_id.replace(3, 1, "_");
  }
  return target_id;
}

auto ParseDeImplementationUuid(const std::string& version) -> std::array<uint8_t, SCMI_TLM_DE_IMPL_UUID_SZ> {
  std::string version_text{version};
  if (version_text.starts_with("0x") || version_text.starts_with("0X")) {
    version_text.erase(0, 2);
  }

  std::string hex_digits;
  hex_digits.reserve(SCMI_TLM_DE_IMPL_UUID_SZ * kHexDigitsPerByte);
  for (unsigned char character : version_text) {
    if (std::isxdigit(character) != 0) {
      hex_digits.push_back(static_cast<char>(character));
    }
  }
  hex_digits.resize(SCMI_TLM_DE_IMPL_UUID_SZ * kHexDigitsPerByte, '0');

  std::array<uint8_t, SCMI_TLM_DE_IMPL_UUID_SZ> bytes{};
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes.at(index) = static_cast<uint8_t>(
        std::stoul(hex_digits.substr(index * kHexDigitsPerByte, kHexDigitsPerByte), nullptr, kHexRadix));
  }
  return bytes;
}

auto DataSize(mock_scmi_value_type_t value_type) -> uint32_t {
  auto data_size = kDefaultDataSize;
  switch (value_type) {
    case MOCK_SCMI_VALUE_UINT8:
    case MOCK_SCMI_VALUE_BOOL8:
      data_size = 1;
      break;
    case MOCK_SCMI_VALUE_UINT16:
      data_size = 2;
      break;
    case MOCK_SCMI_VALUE_UINT32:
    case MOCK_SCMI_VALUE_FLOAT32:
      data_size = 4;
      break;
    case MOCK_SCMI_VALUE_UINT64:
    case MOCK_SCMI_VALUE_FLOAT64:
      data_size = kUint64DataSize;
      break;
    case MOCK_SCMI_VALUE_UNKNOWN:
      break;
  }
  return data_size;
}

auto UnitCode(const std::string& unit) -> uint32_t {
  static const std::unordered_map<std::string, uint32_t> unit_codes = {
      {"None",    0 },
      {"Celsius", 1 },
      {"Percent", 2 },
      {"Count",   3 },
      {"Watts",   4 },
      {"MHz",     5 },
      {"Ticks",   6 },
      {"Seconds", 7 },
      {"Joules",  8 },
      {"Volts",   9 },
      {"Amps",    10},
      {"Bytes",   11},
      {"MB/s",    12},
      {"RPM",     13},
  };

  auto it = unit_codes.find(unit);
  if (it != unit_codes.end()) {
    return it->second;
  }
  return 0;
}

auto UnitExponent(const std::string& unit_exp) -> int32_t {
  if (unit_exp.empty() || unit_exp == "none") {
    return 0;
  }

  try {
    return static_cast<int32_t>(std::stoi(unit_exp));
  } catch (const std::exception&) {
    return 0;
  }
}

auto ActiveInterval(const update_interval_t& interval) -> scmi_tlm_update_interval {
  return scmi_tlm_update_interval{
      .secs = static_cast<uint32_t>(interval.first.count()),
      .exp  = interval.second,
  };
}

auto BuildAbiInfo(SCMITelemetryTarget& tlm) -> scmi_tlm_abi_info {
  scmi_tlm_abi_info info{};
  info.size        = sizeof(info);
  info.abi_version = SCMI_TLM_CURRENT_ABI_VERSION;

  const auto implementation_uuid = ParseDeImplementationUuid(tlm.GetDEImplementationVersion());
  std::span<uint8_t, SCMI_TLM_DE_IMPL_UUID_SZ> de_impl_version{info.primary_de_impl_version};
  std::ranges::copy(implementation_uuid, de_impl_version.begin());

  info.num_des       = static_cast<uint32_t>(tlm.GetDataEvents().size());
  info.num_groups    = static_cast<uint32_t>(tlm.GetGroups().size());
  info.num_intervals = static_cast<uint32_t>(tlm.GetAvailableUpdateIntervals().size());
  info.num_shmtis    = 0;

  info.abi_features = SCMI_TLM_ABI_FEAT_BATCH_STATE | SCMI_TLM_ABI_FEAT_BATCHED_CFG | SCMI_TLM_ABI_FEAT_DE_TRACKING;
  info.features     = 0;
  return info;
}

auto GetTargetConfig(const SCMITelemetryTarget& tlm) -> scmi_tlm_config {
  scmi_tlm_config config{};
  config.enable   = static_cast<uint8_t>(tlm.GetTlmEnableFlag() ? 1U : 0U);
  config.t_enable = static_cast<uint8_t>(tlm.GetAllDesTstampEnableFlag() ? 1U : 0U);
  config.active   = ActiveInterval(tlm.GetCurrentUpdateInterval());
  return config;
}

auto SetTargetConfig(SCMITelemetryTarget& tlm, const scmi_tlm_config& config) -> scmi_tlm_config {
  tlm.SetTlmEnableFlag(config.enable != 0);
  return GetTargetConfig(tlm);
}

auto GetDataEventConfig(const SCMITelemetryTarget& tlm, const DataEvent& data_event) -> scmi_tlm_de_config {
  scmi_tlm_de_config config{};
  config.id       = data_event.id_;
  config.enable   = data_event.enable_ ? 1U : 0U;
  config.t_enable = data_event.tstamp_enable_ ? 1U : 0U;
  if (data_event.enable_) {
    config.sid                     = SCMI_TLM_SHMTI_ID_INVALID;
    const auto implementation_uuid = ParseDeImplementationUuid(tlm.GetDEImplementationVersion());
    auto       config_uuid         = std::span{config.uuid};
    std::ranges::copy(implementation_uuid, config_uuid.begin());
  }
  return config;
}

auto SetDataEventConfig(const SCMITelemetryTarget& tlm, DataEvent& data_event,
                        const scmi_tlm_de_config& requested_config) -> scmi_tlm_de_config {
  data_event.enable_        = requested_config.enable != 0;
  data_event.tstamp_enable_ = requested_config.t_enable != 0;
  return GetDataEventConfig(tlm, data_event);
}

auto GetDataEventInfo(const DataEvent& data_event) -> scmi_tlm_de_info {
  scmi_tlm_de_info info{};
  info.id                                              = data_event.id_;
  info.grp_id                                          = data_event.group_id_.value_or(0);
  info.data_sz                                         = DataSize(data_event.value_type_);
  info.type                                            = data_event.type_;
  info.unit                                            = UnitCode(data_event.unit_);
  info.unit_exp                                        = UnitExponent(data_event.unit_exp_);
  info.ts_rate                                         = data_event.tstamp_rate_;
  info.instance_id                                     = data_event.instance_id_;
  info.compo_instance_id                               = data_event.compo_instance_id_;
  info.compo_type                                      = data_event.compo_type_;
  info.persistent                                      = static_cast<uint8_t>(data_event.persistent_ ? 1U : 0U);
  const auto                            formatted_name = std::format("de_{:08x}", data_event.id_);
  const auto                            name_size      = std::min(formatted_name.size(), sizeof(info.name) - 1);
  std::span<uint8_t, sizeof(info.name)> name{info.name};
  std::ranges::copy_n(formatted_name.begin(), static_cast<std::ptrdiff_t>(name_size), name.begin());
  return info;
}

auto ReadDataEventSample(const SCMITelemetryTarget& tlm, DataEvent& data_event) -> scmi_tlm_de_sample {
  scmi_tlm_de_sample sample{};
  sample.id = data_event.id_;
  if (!tlm.GetTlmEnableFlag() || !data_event.enable_) {
    return sample;
  }

  data_event.last_value_ = data_event.Generate();
  sample.tstamp          = data_event.tstamp_enable_ ? data_event.GetScaledTimestamp() : 0U;
  sample.val             = data_event.last_value_.ui64;
  return sample;
}

auto GetGroupInfo(SCMITelemetryTarget& tlm, const scmi_tlm_grp_info& requested_info) -> IoctlReply {
  const auto group = tlm.GetGroups().find(requested_info.grp_id);
  if (group == tlm.GetGroups().end()) {
    return {.error_code = EINVAL};
  }

  scmi_tlm_grp_info info{};
  info.grp_id        = requested_info.grp_id;
  info.num_des       = static_cast<uint32_t>(group->second->des_.size());
  info.num_intervals = static_cast<uint32_t>(group->second->intervals_.update_intervals.size());
  return ReplyWith(info);
}

auto SetAllDataEventConfig(SCMITelemetryTarget& tlm, const scmi_tlm_de_config& requested_config) -> scmi_tlm_de_config {
  for (const auto& event : tlm.GetDataEvents()) {
    event->enable_        = requested_config.enable != 0;
    event->tstamp_enable_ = requested_config.t_enable != 0;
  }
  tlm.SetAllDesEnableFlag(requested_config.enable != 0);
  tlm.SetAllDesTstampEnableFlag(requested_config.t_enable != 0);

  scmi_tlm_de_config config{};
  config.enable   = requested_config.enable != 0 ? 1U : 0U;
  config.t_enable = requested_config.t_enable != 0 ? 1U : 0U;
  return config;
}

template <typename T, typename Handler>
auto WithInput(const void* in_buf, size_t in_bufsz, Handler handler) -> IoctlReply {
  T request{};
  if (!ReadInput(in_buf, in_bufsz, request)) {
    return {.error_code = EINVAL};
  }
  return handler(request);
}

template <typename T, typename Handler>
auto WithDataEvent(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz, Handler handler) -> IoctlReply {
  return WithInput<T>(in_buf, in_bufsz, [&tlm, handler](const T& request) {
    DataEvent* data_event = tlm.GetDataEventById(request.id);
    if (data_event == nullptr) {
      return IoctlReply{.error_code = EINVAL};
    }
    return handler(request, *data_event);
  });
}

auto HandleGetAbiInfo(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  uint32_t requested_size{};
  if (!ReadInput(in_buf, in_bufsz, requested_size)) {
    return {.error_code = EFAULT};
  }
  if (requested_size < sizeof(scmi_tlm_abi_info)) {
    return {.error_code = EINVAL};
  }
  if (in_bufsz < sizeof(scmi_tlm_abi_info)) {
    return {.error_code = EFAULT};
  }
  const auto available_size = std::min(static_cast<size_t>(requested_size), in_bufsz);
  const auto input          = std::span{static_cast<const unsigned char*>(in_buf), available_size};
  if (std::ranges::any_of(input.subspan(sizeof(scmi_tlm_abi_info)), [](unsigned char byte) { return byte != 0; })) {
    return {.error_code = EFAULT};
  }

  const auto abi_info = BuildAbiInfo(tlm);
  IoctlReply reply{};
  reply.output.resize(available_size);
  std::memcpy(reply.output.data(), &abi_info, sizeof(abi_info));
  return reply;
}

// cppcheck-suppress constParameterReference // Ioctl handlers share a callback
// signature with mutating handlers.
auto HandleGetConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  (void)in_buf;
  (void)in_bufsz;
  return ReplyWith(GetTargetConfig(tlm));
}

auto HandleSetConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return WithInput<scmi_tlm_config>(
      in_buf, in_bufsz, [&tlm](const scmi_tlm_config& config) { return ReplyWith(SetTargetConfig(tlm, config)); });
}

auto HandleDataEventConfigBatch(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz, bool set) -> IoctlReply {
  scmi_tlm_batch batch{};
  if (!ReadInput(in_buf, in_bufsz, batch) || batch.reserved != 0 || batch.item_sz != sizeof(scmi_tlm_de_config)) {
    return {.error_code = EINVAL};
  }
  if (batch.num_items > std::numeric_limits<size_t>::max() / batch.item_sz) {
    return {.error_code = EINVAL};
  }

  const size_t items_size  = static_cast<size_t>(batch.num_items) * batch.item_sz;
  const size_t states_size = batch.states != 0 ? static_cast<size_t>(batch.num_items) * sizeof(int32_t) : 0;
  if (items_size > std::numeric_limits<size_t>::max() - sizeof(batch) ||
      states_size > std::numeric_limits<size_t>::max() - sizeof(batch) - items_size) {
    return {.error_code = EINVAL};
  }
  const size_t flattened_size = sizeof(batch) + items_size + states_size;
  if (in_buf == nullptr || in_bufsz < flattened_size) {
    return {.error_code = EFAULT};
  }

  std::vector<scmi_tlm_de_config> configs(batch.num_items);
  const auto                      input = std::span{static_cast<const std::byte*>(in_buf), flattened_size};
  std::memcpy(configs.data(), input.subspan(sizeof(batch), items_size).data(), items_size);
  std::vector<int32_t> states(batch.num_items);

  for (uint32_t index = 0; index < batch.num_items; ++index) {
    auto* data_event = tlm.GetDataEventById(configs[index].id);
    if (data_event == nullptr) {
      if (batch.states == 0) {
        return {.error_code = EINVAL};
      }
      states[index] = -EINVAL;
      continue;
    }
    configs[index] = set ? SetDataEventConfig(tlm, *data_event, configs[index]) : GetDataEventConfig(tlm, *data_event);
  }

  IoctlReply reply{};
  reply.output.resize(flattened_size);
  auto output = std::span{reply.output};
  std::memcpy(output.data(), &batch, sizeof(batch));
  std::memcpy(output.subspan(sizeof(batch), items_size).data(), configs.data(), items_size);
  if (states_size != 0) {
    std::memcpy(output.subspan(sizeof(batch) + items_size, states_size).data(), states.data(), states_size);
  }
  return reply;
}

auto HandleGetDataEventConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return HandleDataEventConfigBatch(tlm, in_buf, in_bufsz, false);
}

auto HandleSetDataEventConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return HandleDataEventConfigBatch(tlm, in_buf, in_bufsz, true);
}

auto HandleGetDataEventInfo(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return WithDataEvent<scmi_tlm_de_info>(
      tlm, in_buf, in_bufsz,
      [](const scmi_tlm_de_info&, const DataEvent& data_event) { return ReplyWith(GetDataEventInfo(data_event)); });
}

auto HandleGetDataEventValue(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return WithDataEvent<scmi_tlm_de_sample>(tlm, in_buf, in_bufsz,
                                           [&tlm](const scmi_tlm_de_sample&, DataEvent& data_event) {
                                             return ReplyWith(ReadDataEventSample(tlm, data_event));
                                           });
}

// cppcheck-suppress constParameterReference // Ioctl handlers share a callback
// signature with mutating handlers.
auto HandleGetAllConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  (void)in_buf;
  (void)in_bufsz;
  scmi_tlm_de_config config{};
  config.enable   = tlm.GetAllDesEnableFlag() ? 1U : 0U;
  config.t_enable = tlm.GetAllDesTstampEnableFlag() ? 1U : 0U;
  return ReplyWith(config);
}

auto HandleSetAllConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return WithInput<scmi_tlm_de_config>(in_buf, in_bufsz, [&tlm](const scmi_tlm_de_config& config) {
    return ReplyWith(SetAllDataEventConfig(tlm, config));
  });
}

auto HandleGetGroupInfoCommand(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return WithInput<scmi_tlm_grp_info>(in_buf, in_bufsz,
                                      [&tlm](const scmi_tlm_grp_info& info) { return GetGroupInfo(tlm, info); });
}

using IoctlCommandHandler = IoctlReply (*)(SCMITelemetryTarget&, const void*, size_t);

auto FindIoctlCommandHandler(unsigned int cmd) -> IoctlCommandHandler {
  static const std::unordered_map<unsigned int, IoctlCommandHandler> handlers = {
      {SCMI_TLM_GET_ABI_INFO, HandleGetAbiInfo         },
      {SCMI_TLM_GET_CFG,      HandleGetConfig          },
      {SCMI_TLM_SET_CFG,      HandleSetConfig          },
      {SCMI_TLM_GET_DE_CFG,   HandleGetDataEventConfig },
      {SCMI_TLM_SET_DE_CFG,   HandleSetDataEventConfig },
      {SCMI_TLM_GET_DE_INFO,  HandleGetDataEventInfo   },
      {SCMI_TLM_DE_READ,      HandleGetDataEventValue  },
      {SCMI_TLM_GET_ALL_CFG,  HandleGetAllConfig       },
      {SCMI_TLM_SET_ALL_CFG,  HandleSetAllConfig       },
      {SCMI_TLM_GET_GRP_INFO, HandleGetGroupInfoCommand},
  };

  const auto it = handlers.find(cmd);
  return it != handlers.end() ? it->second : nullptr;
}

}  // namespace

auto InitProtocolScmiIoctl(FileSystemNode* g_root, const std::optional<std::string>& config_file_path) -> void {
  const auto tlm_ids = InitScmiTelemetryTargets(config_file_path);
  for (const auto& tlm_id : tlm_ids) {
    g_root->AddChild(FileSystemNode::CreateFile(ToDeviceName(tlm_id), "", FileAccess::READ_WRITE, g_root,
                                                ProtocolType::SCMI_IOCTL_TELEMETRY));
  }
}

auto HandleProtocolScmiIoctl(const std::string& target_id, unsigned int cmd, const void* in_buf, size_t in_bufsz)
    -> IoctlReply {
  auto& tlm = SCMITelemetryTarget::Instance(target_id);

  const auto handler = FindIoctlCommandHandler(cmd);
  if (handler == nullptr) {
    return {.error_code = ENOTTY};
  }
  return handler(tlm, in_buf, in_bufsz);
}

}  // namespace mock_scmi
