// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "protocol_scmi_ioctl.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <unordered_map>

#include "common.hpp"
#include "fsnode.hpp"
#include "protocol_telemetry.hpp"
#include "protocol_type.hpp"
#include "scmi_ioctl_uapi.hpp"

namespace mock_scmi {
namespace {

constexpr uint32_t kMajorVersionShift = 16;
constexpr uint32_t kDefaultDataSize   = 8;
constexpr uint32_t kUint64DataSize    = 8;
constexpr size_t   kHexDigitsPerWord  = 8;
constexpr size_t   kHexDigitsPerByte  = 2;

template <typename T>
auto ReplyWith(const T& value) -> IoctlReply {
  IoctlReply reply{};
  reply.output.resize(sizeof(T));
  std::memcpy(reply.output.data(), &value, sizeof(T));
  return reply;
}

template <typename T>
auto ReadInput(const void* in_buf, size_t in_bufsz, T& value) -> bool {
  if (in_buf == nullptr || in_bufsz < sizeof(T)) {
    return false;
  }
  std::memcpy(&value, in_buf, sizeof(T));
  return true;
}

auto ToDeviceName(std::string target_id) -> std::string {
  if (target_id.starts_with("tlm-")) {
    target_id.replace(3, 1, "_");
  }
  return target_id;
}

auto ToTargetId(std::string device_name) -> std::string {
  if (device_name.starts_with("tlm_")) {
    device_name.replace(3, 1, "-");
  }
  return device_name;
}

auto ParseVersion(const std::string& version) -> uint32_t {
  uint32_t major = 0;
  uint32_t minor = 0;

  const auto dot             = version.find('.');
  auto       parse_component = [](std::string_view text, uint32_t& output) {
    const auto* begin = text.data();
    const auto* end   = text.data() + text.size();
    std::from_chars(begin, end, output);
  };

  parse_component(std::string_view{version}.substr(0, dot), major);
  if (dot != std::string::npos) {
    parse_component(std::string_view{version}.substr(dot + 1), minor);
  }
  return (major << kMajorVersionShift) | minor;
}

auto ParseDeImplementationVersion(const std::string& version) -> std::array<uint32_t, SCMI_TLM_DE_IMPL_MAX_DWORDS> {
  std::string version_text{version};
  if (version_text.starts_with("0x") || version_text.starts_with("0X")) {
    version_text.erase(0, 2);
  }

  std::string hex_digits;
  hex_digits.reserve(SCMI_TLM_DE_IMPL_MAX_DWORDS * kHexDigitsPerWord);
  for (unsigned char character : version_text) {
    if (std::isxdigit(character) != 0) {
      hex_digits.push_back(static_cast<char>(character));
    }
  }

  hex_digits.resize(SCMI_TLM_DE_IMPL_MAX_DWORDS * kHexDigitsPerWord, '0');

  std::array<uint32_t, SCMI_TLM_DE_IMPL_MAX_DWORDS> words{};
  for (size_t index = 0; index < words.size(); ++index) {
    words.at(index) = static_cast<uint32_t>(
        std::stoul(hex_digits.substr(index * kHexDigitsPerWord, kHexDigitsPerWord), nullptr, kHexRadix));
  }
  return words;
}

auto ParseDeImplementationUuid(const std::string& version) -> std::array<uint8_t, SCMI_TLM_DE_IMPL_UUID_MAX> {
  std::string version_text{version};
  if (version_text.starts_with("0x") || version_text.starts_with("0X")) {
    version_text.erase(0, 2);
  }

  std::string hex_digits;
  hex_digits.reserve(SCMI_TLM_DE_IMPL_UUID_MAX * kHexDigitsPerByte);
  for (unsigned char character : version_text) {
    if (std::isxdigit(character) != 0) {
      hex_digits.push_back(static_cast<char>(character));
    }
  }
  hex_digits.resize(SCMI_TLM_DE_IMPL_UUID_MAX * kHexDigitsPerByte, '0');

  std::array<uint8_t, SCMI_TLM_DE_IMPL_UUID_MAX> bytes{};
  for (size_t index = 0; index < bytes.size(); ++index) {
    bytes.at(index) = static_cast<uint8_t>(
        std::stoul(hex_digits.substr(index * kHexDigitsPerByte, kHexDigitsPerByte), nullptr, kHexRadix));
  }
  return bytes;
}

auto DataSize(astl_value_type_t value_type) -> uint32_t {
  auto data_size = kDefaultDataSize;
  switch (value_type) {
    case ASTL_VALUE_UINT8:
    case ASTL_VALUE_BOOL8:
      data_size = 1;
      break;
    case ASTL_VALUE_UINT16:
      data_size = 2;
      break;
    case ASTL_VALUE_UINT32:
    case ASTL_VALUE_FLOAT32:
      data_size = 4;
      break;
    case ASTL_VALUE_UINT64:
    case ASTL_VALUE_FLOAT64:
      data_size = kUint64DataSize;
      break;
    case ASTL_VALUE_UNKNOWN:
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

auto BuildBaseInfo(SCMITelemetryTarget& tlm) -> scmi_tlm_base_info {
  scmi_tlm_base_info info{};
  info.version       = ParseVersion(tlm.GetVersion());
  const auto version = ParseDeImplementationVersion(tlm.GetDEImplementationVersion());
  std::span<uint32_t, SCMI_TLM_DE_IMPL_MAX_DWORDS> de_impl_version{info.de_impl_version};
  std::ranges::copy(version, de_impl_version.begin());
  info.num_des       = static_cast<uint32_t>(tlm.GetDataEvents().size());
  info.num_groups    = static_cast<uint32_t>(tlm.GetGroups().size());
  info.num_intervals = static_cast<uint32_t>(tlm.GetAvailableUpdateIntervals().size());
  info.num_shmtis    = 0;
  info.flags         = 0;
  return info;
}

auto BuildAbiInfo(SCMITelemetryTarget& tlm) -> scmi_tlm_abi_info {
  scmi_tlm_abi_info info{};
  info.size        = sizeof(info);
  info.abi_version = SCMI_TLM_CURRENT_ABI_VERSION;

  const auto implementation_uuid = ParseDeImplementationUuid(tlm.GetDEImplementationVersion());
  std::span<uint8_t, SCMI_TLM_DE_IMPL_UUID_MAX> de_impl_version{info.de_impl_version};
  std::ranges::copy(implementation_uuid, de_impl_version.begin());

  info.num_des       = static_cast<uint32_t>(tlm.GetDataEvents().size());
  info.num_groups    = static_cast<uint32_t>(tlm.GetGroups().size());
  info.num_intervals = static_cast<uint32_t>(tlm.GetAvailableUpdateIntervals().size());
  info.num_shmtis    = 0;

  // Do not advertise optional operations until the mock implements their
  // complete behavior. V6 clients use these bits to choose safe fallbacks.
  info.abi_features = SCMI_TLM_ABI_FEAT_RESET;
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

auto GetDataEventConfig(const DataEvent& data_event) -> scmi_tlm_de_config {
  scmi_tlm_de_config config{};
  config.id       = data_event.id_;
  config.enable   = data_event.enable_ ? 1U : 0U;
  config.t_enable = data_event.tstamp_enable_ ? 1U : 0U;
  return config;
}

auto SetDataEventConfig(DataEvent& data_event, const scmi_tlm_de_config& requested_config) -> scmi_tlm_de_config {
  data_event.enable_        = requested_config.enable != 0;
  data_event.tstamp_enable_ = requested_config.t_enable != 0;
  return GetDataEventConfig(data_event);
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

auto HandleGetInfo(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  (void)in_buf;
  (void)in_bufsz;
  return ReplyWith(BuildBaseInfo(tlm));
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
  const auto* bytes          = static_cast<const unsigned char*>(in_buf);
  const auto  available_size = std::min(static_cast<size_t>(requested_size), in_bufsz);
  if (std::ranges::any_of(std::span{bytes + sizeof(scmi_tlm_abi_info), available_size - sizeof(scmi_tlm_abi_info)},
                          [](unsigned char byte) { return byte != 0; })) {
    return {.error_code = EFAULT};
  }

  const auto abi_info = BuildAbiInfo(tlm);
  IoctlReply reply{};
  reply.output.resize(available_size);
  std::memcpy(reply.output.data(), &abi_info, sizeof(abi_info));
  return reply;
}

// cppcheck-suppress constParameterReference // Ioctl handlers share a callback signature with mutating handlers.
auto HandleGetConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  (void)in_buf;
  (void)in_bufsz;
  return ReplyWith(GetTargetConfig(tlm));
}

auto HandleSetConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return WithInput<scmi_tlm_config>(
      in_buf, in_bufsz, [&tlm](const scmi_tlm_config& config) { return ReplyWith(SetTargetConfig(tlm, config)); });
}

auto HandleGetDataEventConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return WithDataEvent<scmi_tlm_de_config>(
      tlm, in_buf, in_bufsz,
      [](const scmi_tlm_de_config&, const DataEvent& data_event) { return ReplyWith(GetDataEventConfig(data_event)); });
}

auto HandleSetDataEventConfig(SCMITelemetryTarget& tlm, const void* in_buf, size_t in_bufsz) -> IoctlReply {
  return WithDataEvent<scmi_tlm_de_config>(tlm, in_buf, in_bufsz,
                                           [](const scmi_tlm_de_config& config, DataEvent& data_event) {
                                             return ReplyWith(SetDataEventConfig(data_event, config));
                                           });
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

// cppcheck-suppress constParameterReference // Ioctl handlers share a callback signature with mutating handlers.
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
      {SCMI_TLM_GET_INFO,     HandleGetInfo            },
      {SCMI_TLM_GET_CFG,      HandleGetConfig          },
      {SCMI_TLM_SET_CFG,      HandleSetConfig          },
      {SCMI_TLM_GET_DE_CFG,   HandleGetDataEventConfig },
      {SCMI_TLM_SET_DE_CFG,   HandleSetDataEventConfig },
      {SCMI_TLM_GET_DE_INFO,  HandleGetDataEventInfo   },
      {SCMI_TLM_GET_DE_VALUE, HandleGetDataEventValue  },
      {SCMI_TLM_GET_ALL_CFG,  HandleGetAllConfig       },
      {SCMI_TLM_SET_ALL_CFG,  HandleSetAllConfig       },
      {SCMI_TLM_GET_GRP_INFO, HandleGetGroupInfoCommand},
  };

  const auto it = handlers.find(cmd);
  return it != handlers.end() ? it->second : nullptr;
}

auto IsGetAbiInfoCommand(unsigned int cmd) -> bool {
  return _IOC_TYPE(cmd) == SCMI_TLM_IOCTL_MAGIC && _IOC_NR(cmd) == _IOC_NR(SCMI_TLM_GET_ABI_INFO) &&
         _IOC_DIR(cmd) == (_IOC_READ | _IOC_WRITE) && _IOC_SIZE(cmd) >= sizeof(uint32_t);
}

}  // namespace

auto InitProtocolScmiIoctl(FileSystemNode* g_root, const std::optional<std::string>& config_file_path) -> void {
  const auto tlm_ids = InitScmiTelemetryTargets(config_file_path);
  for (const auto& tlm_id : tlm_ids) {
    g_root->AddChild(FileSystemNode::CreateFile(ToDeviceName(tlm_id), "", FileAccess::READ_WRITE, g_root,
                                                ProtocolType::SCMI_IOCTL_TELEMETRY));
  }
}

auto HandleProtocolScmiIoctl(const FileSystemNode* node, unsigned int cmd, const void* in_buf, size_t in_bufsz)
    -> IoctlReply {
  auto& tlm = SCMITelemetryTarget::Instance(ToTargetId(node->GetName()));

  // The encoded ioctl size changes when userspace knows a newer ABI-info
  // suffix. Dispatch request 0 by its stable type/number/direction and use the
  // leading size field to negotiate the common V1 prefix.
  if (IsGetAbiInfoCommand(cmd)) {
    return HandleGetAbiInfo(tlm, in_buf, in_bufsz);
  }

  const auto handler = FindIoctlCommandHandler(cmd);
  if (handler == nullptr) {
    return {.error_code = ENOTTY};
  }
  return handler(tlm, in_buf, in_bufsz);
}

}  // namespace mock_scmi
