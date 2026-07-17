// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "protocol_telemetry.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <unordered_map>
#include <vector>

#include "common.hpp"
#include "config_protocol_telemetry.hpp"
#include "fsnode.hpp"
#include "protocol_telemetry_internal.hpp"
#include "protocol_type.hpp"

namespace mock_scmi {

std::unique_ptr<FileSystemNode> BuildProtocolTelemetryFileTree(FileSystemNode*    arm_telemetry_root,
                                                               std::string const& tlm_id);

static std::unordered_map<std::string, std::unique_ptr<SCMITelemetryTarget>>& Instances() {
  static std::unordered_map<std::string, std::unique_ptr<SCMITelemetryTarget>> tlm_to_target;
  return tlm_to_target;
}

static auto ResolveTlmJsonPath(const std::optional<std::string>& cli_tlm_json_path) -> std::string {
  if (cli_tlm_json_path.has_value()) {
    return *cli_tlm_json_path;
  }

  const char* env = std::getenv("ASTL_MOCKSCMI_TLM_JSON_PATH");
  if (env == nullptr) {
    throw std::runtime_error("ASTL_MOCKSCMI_TLM_JSON_PATH environment variable is not set.");
  }

  return env;
}

auto InitScmiTelemetryTargets(const std::optional<std::string>& cli_tlm_json_path) -> std::vector<std::string> {
  auto& tlm_to_tgt = Instances();
  tlm_to_tgt.clear();

  const auto tlm_json_path = ResolveTlmJsonPath(cli_tlm_json_path);

  std::ifstream tlm_json_file(tlm_json_path);
  if (!tlm_json_file) {
    throw std::runtime_error("Could not open file: " + tlm_json_path);
  }
  const json tlm_json = json::parse(tlm_json_file);

  std::cout << "tlm schema version: " << tlm_json.at("schema_version") << "\n";

  std::vector<std::string> tlm_ids;
  for (const auto& target : tlm_json.at("targets")) {
    const auto&           settings              = target["settings"];
    const auto&           tlm_id                = static_cast<std::string>(target["instance"]);
    const auto&           all_des_enable        = static_cast<bool>(settings["all_des_enable"]);
    const auto&           all_des_tstamp_enable = static_cast<bool>(settings["all_des_tstamp_enable"]);
    const auto&           tlm_enable            = static_cast<bool>(settings["tlm_enable"]);
    const auto&           telemetry_version     = static_cast<std::string>(settings["telemetry_version"]);
    const auto&           de_version            = static_cast<std::string>(settings["data_event_version"]);
    std::string           raw_data_events_path  = settings.at("data_events_path").get<std::string>();
    std::filesystem::path data_events_path = std::filesystem::path(tlm_json_path).parent_path() / raw_data_events_path;

    UpdateInterval update_intervals{};
    for (const auto& interval : settings.at("update_intervals")) {
      auto secs = std::chrono::seconds(interval.at(0).get<int64_t>());
      auto exp  = interval.at(1).get<int16_t>();
      update_intervals.update_intervals.emplace_back(secs, exp);
    }

    auto secs = std::chrono::seconds(settings["active_update_interval"].at(0).get<int64_t>());
    auto exp  = settings["active_update_interval"].at(1).get<int16_t>();
    update_intervals.active_update_interval = std::make_pair(secs, exp);

    auto tlm = std::make_unique<SCMITelemetryTarget>(SCMITelemetryTargetConfig{
        .all_des_enable            = all_des_enable,
        .all_des_tstamp_enable     = all_des_tstamp_enable,
        .intervals                 = update_intervals,
        .tlm_enable                = tlm_enable,
        .version                   = telemetry_version,
        .de_implementation_version = de_version,
        .data_events               = CreateTelemetryDataEvents(data_events_path, update_intervals),
    });
    tlm_to_tgt.emplace(tlm_id, std::move(tlm));
    tlm_ids.push_back(tlm_id);
  }

  return tlm_ids;
}

auto InitProtocolTelemetry(const FileSystemNode* g_root, const std::optional<std::string>& cli_tlm_json_path)
    -> std::unique_ptr<FileSystemNode> {
  const auto tlm_ids = InitScmiTelemetryTargets(cli_tlm_json_path);

  auto arm_telemetry = FileSystemNode::CreateDirectory("arm_telemetry", g_root, ProtocolType::SCMI_SYSFS_TELEMETRY);

  for (const auto& cur_tlm_id : tlm_ids) {
    auto tlm_file_node = BuildProtocolTelemetryFileTree(arm_telemetry.get(), cur_tlm_id);
    arm_telemetry->AddChild(std::move(tlm_file_node));
  }

  return arm_telemetry;
};

auto GetInitialIntervalValue(const SCMITelemetryTarget& tlm) -> std::string {
  std::string initial_value;

  const auto& intervals = tlm.GetAvailableUpdateIntervals();
  if (tlm.GetIntervalsAreDiscreteFlag()) {
    for (const auto& [secs, exp] : intervals) {
      initial_value += std::format("{},{} ", ToRaw<std::chrono::seconds>(secs), exp);
    }
    if (!initial_value.empty()) {
      initial_value.pop_back();  // Remove trailing space
    }
  } else {
    const auto& min_interval = GetInterval(intervals, std::ranges::min_element);
    const auto& max_interval = GetInterval(intervals, std::ranges::max_element);
    const auto& step         = tlm.GetStepSize();

    initial_value = std::format("{},{} {},{} {},{}", ToRawString<std::chrono::seconds>(min_interval.first),
                                min_interval.second, ToRawString<std::chrono::seconds>(max_interval.first),
                                max_interval.second, ToRawString<std::chrono::seconds>(step.first), step.second);
  }

  return initial_value;
}

auto BuildTelemetryTopLevelFiles(SCMITelemetryTarget const& tlm, FileSystemNode* telemetry) -> void {
  telemetry->AddChild(
      FileSystemNode::CreateFile("all_des_enable", std::to_string(static_cast<int>(tlm.GetAllDesEnableFlag())) + "\n",
                                 FileAccess::WRITE_ONLY, telemetry, ProtocolType::SCMI_SYSFS_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile(
      "all_des_tstamp_enable", std::to_string(static_cast<int>(tlm.GetAllDesTstampEnableFlag())) + "\n",
      FileAccess::WRITE_ONLY, telemetry, ProtocolType::SCMI_SYSFS_TELEMETRY));

  const auto& active_update_interval = tlm.GetCurrentUpdateInterval();
  auto formatted_interval = std::format("{},{}\n", ToRawString<std::chrono::seconds>(active_update_interval.first),
                                        active_update_interval.second);
  telemetry->AddChild(FileSystemNode::CreateFile("current_update_interval_ms", formatted_interval,
                                                 FileAccess::READ_WRITE, telemetry,
                                                 ProtocolType::SCMI_SYSFS_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("des_bulk_read", "", FileAccess::READ_ONLY, telemetry,
                                                 ProtocolType::SCMI_SYSFS_TELEMETRY));

  telemetry->AddChild(
      FileSystemNode::CreateFile("tlm_enable", std::to_string(static_cast<int>(tlm.GetTlmEnableFlag())) + "\n",
                                 FileAccess::READ_WRITE, telemetry, ProtocolType::SCMI_SYSFS_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("version", tlm.GetVersion() + "\n", FileAccess::READ_ONLY, telemetry,
                                                 ProtocolType::SCMI_SYSFS_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("de_implementation_version", tlm.GetDEImplementationVersion() + "\n",
                                                 FileAccess::READ_ONLY, telemetry, ProtocolType::SCMI_SYSFS_TELEMETRY));

  telemetry->AddChild(FileSystemNode::CreateFile("des_single_sample_read", "", FileAccess::READ_ONLY, telemetry,
                                                 ProtocolType::SCMI_SYSFS_TELEMETRY));

  std::string initial_value = GetInitialIntervalValue(tlm);
  telemetry->AddChild(FileSystemNode::CreateFile("available_update_intervals_ms", initial_value + "\n",
                                                 FileAccess::READ_WRITE, telemetry,
                                                 ProtocolType::SCMI_SYSFS_TELEMETRY));

  telemetry->AddChild(
      FileSystemNode::CreateFile("intervals_discrete", tlm.GetIntervalsAreDiscreteFlag() ? "1\n" : "0\n",
                                 FileAccess::READ_WRITE, telemetry, ProtocolType::SCMI_SYSFS_TELEMETRY));
};

auto BuildTelemetryDesFiles(SCMITelemetryTarget const& tlm, FileSystemNode* telemetry) -> void {
  auto des_dir = FileSystemNode::CreateDirectory("des", telemetry, ProtocolType::SCMI_SYSFS_TELEMETRY);
  // For each data event, create its event directory and add files.
  for (const auto& data_event : tlm.GetDataEvents()) {
    std::string id_str = std::format("0x{:08X}", data_event->id_);

    auto event_dir = FileSystemNode::CreateDirectory(id_str, des_dir.get(), ProtocolType::SCMI_SYSFS_TELEMETRY);

    event_dir->AddChild(FileSystemNode::CreateFile("enable", data_event->enable_ ? "1\n" : "0\n",
                                                   FileAccess::READ_WRITE, event_dir.get(),
                                                   ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("tstamp_enable", data_event->tstamp_enable_ ? "1\n" : "0\n",
                                                   FileAccess::READ_WRITE, event_dir.get(),
                                                   ProtocolType::SCMI_SYSFS_TELEMETRY));

    // TODO(ASTL-116) - Handle all other _astl_value_type_t
    event_dir->AddChild(FileSystemNode::CreateFile(
        "value", std::format("{} {:016x}\n", data_event->GetScaledTimestamp(), data_event->last_value_.ui64),
        FileAccess::READ_ONLY, event_dir.get(), ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(
        FileSystemNode::CreateFile("compo_instance_id", std::to_string(data_event->compo_instance_id_) + "\n",
                                   FileAccess::READ_ONLY, event_dir.get(), ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("compo_type", std::to_string(data_event->compo_type_) + "\n",
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("instance_id", std::to_string(data_event->instance_id_) + "\n",
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("persistent", data_event->persistent_ ? "1\n" : "0\n",
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("tstamp_rate", std::to_string(data_event->tstamp_rate_) + "\n",
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("type", std::to_string(data_event->type_) + "\n",
                                                   FileAccess::READ_ONLY, event_dir.get(),
                                                   ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("unit", data_event->unit_ + "\n", FileAccess::READ_ONLY,
                                                   event_dir.get(), ProtocolType::SCMI_SYSFS_TELEMETRY));

    event_dir->AddChild(FileSystemNode::CreateFile("unit_exp", data_event->unit_exp_ + "\n", FileAccess::READ_ONLY,
                                                   event_dir.get(), ProtocolType::SCMI_SYSFS_TELEMETRY));

    des_dir->AddChild(std::move(event_dir));
  }
  telemetry->AddChild(std::move(des_dir));
}

auto BuildTelemetryGroupFiles(SCMITelemetryTarget& tlm, FileSystemNode* telemetry) -> void {
  auto groups_dir = FileSystemNode::CreateDirectory("groups", telemetry, ProtocolType::SCMI_SYSFS_TELEMETRY);
  for (const auto& [group_id, group] : tlm.GetGroups()) {
    // directory for group n, where "n" is the numeric group ID used
    // as the directory name (e.g., groups/0, groups/1, ...).
    auto group_n_dir =
        FileSystemNode::CreateDirectory(std::to_string(group_id), groups_dir.get(), ProtocolType::SCMI_SYSFS_TELEMETRY);

    // group specific files
    auto composing_des = FileSystemNode::CreateFile("composing_des", "", FileAccess::READ_ONLY, group_n_dir.get(),
                                                    ProtocolType::SCMI_SYSFS_TELEMETRY);

    const auto& active_update_interval = group->intervals_.active_update_interval;
    auto formatted_interval = std::format("{},{}\n", ToRawString<std::chrono::seconds>(active_update_interval.first),
                                          active_update_interval.second);
    group_n_dir->AddChild(FileSystemNode::CreateFile("current_update_interval_ms", formatted_interval,
                                                     FileAccess::READ_WRITE, group_n_dir.get(),
                                                     ProtocolType::SCMI_SYSFS_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("des_bulk_read", "", FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_SYSFS_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("des_single_sample_read", "", FileAccess::READ_ONLY,
                                                     group_n_dir.get(), ProtocolType::SCMI_SYSFS_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("enable", group->enable_ ? "1\n" : "0\n", FileAccess::READ_WRITE,
                                                     group_n_dir.get(), ProtocolType::SCMI_SYSFS_TELEMETRY));

    std::string initial_value = GetInitialIntervalValue(tlm);
    group_n_dir->AddChild(FileSystemNode::CreateFile("available_update_intervals_ms", initial_value + "\n",
                                                     FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_SYSFS_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("intervals_discrete", group->intervals_.discrete ? "1\n" : "0\n",
                                                     FileAccess::READ_ONLY, group_n_dir.get(),
                                                     ProtocolType::SCMI_SYSFS_TELEMETRY));

    group_n_dir->AddChild(FileSystemNode::CreateFile("tstamp_enable", group->tstamp_enable_ ? "1\n" : "0\n",
                                                     FileAccess::READ_WRITE, group_n_dir.get(),
                                                     ProtocolType::SCMI_SYSFS_TELEMETRY));

    // de specific changes
    for (auto event : group->des_) {
      auto& file_content = composing_des->GetFileContent();
      file_content += std::format("0x{:08X} ", event);
    }

    if (!composing_des->GetFileContent().empty()) {
      composing_des->GetFileContent().pop_back();  // Remove trailing space
    }

    group_n_dir->AddChild(std::move(composing_des));

    groups_dir->AddChild(std::move(group_n_dir));
  };
  telemetry->AddChild(std::move(groups_dir));
};

auto BuildProtocolTelemetryFileTree(FileSystemNode* arm_telemetry_root, const std::string& tlm_id)
    -> std::unique_ptr<FileSystemNode> {
  auto& tlm = SCMITelemetryTarget::Instance(tlm_id);

  // Create tlm-N under arm_telemetry_root.
  auto telemetry = FileSystemNode::CreateDirectory(tlm_id, arm_telemetry_root, ProtocolType::SCMI_SYSFS_TELEMETRY);

  BuildTelemetryTopLevelFiles(tlm, telemetry.get());
  BuildTelemetryDesFiles(tlm, telemetry.get());
  BuildTelemetryGroupFiles(tlm, telemetry.get());

  return telemetry;
}

auto GetTelemetryFile(const std::string& name) -> TelemetryFile {
  static const std::unordered_map<std::string, TelemetryFile> lookup = {
      {"all_des_enable",                TelemetryFile::ALL_DES_ENABLE               },
      {"all_des_tstamp_enable",         TelemetryFile::ALL_DES_TSTAMP_ENABLE        },
      {"current_update_interval_ms",    TelemetryFile::CURRENT_UPDATE_INTERVAL_MS   },
      {"des_bulk_read",                 TelemetryFile::DES_BULK_READ                },
      {"des_single_sample_read",        TelemetryFile::DES_SINGLE_SAMPLE_READ       },
      {"tlm_enable",                    TelemetryFile::TLM_ENABLE                   },
      {"version",                       TelemetryFile::VERSION                      },
      {"de_implementation_version",     TelemetryFile::DE_IMPLEMENTATION_VERSION    },
      {"available_update_intervals_ms", TelemetryFile::AVAILABLE_UPDATE_INTERVALS_MS},
      {"intervals_discrete",            TelemetryFile::INTERVALS_DISCRETE           },
      {"enable",                        TelemetryFile::ENABLE                       },
      {"compo_instance_id",             TelemetryFile::COMPO_INSTANCE_ID            },
      {"compo_type",                    TelemetryFile::COMPO_TYPE                   },
      {"instance_id",                   TelemetryFile::INSTANCE_ID                  },
      {"persistent",                    TelemetryFile::PERSISTENT                   },
      {"tstamp_rate",                   TelemetryFile::TSTAMP_RATE                  },
      {"type",                          TelemetryFile::TYPE                         },
      {"unit",                          TelemetryFile::UNIT                         },
      {"unit_exp",                      TelemetryFile::UNIT_EXP                     },
      {"tstamp_enable",                 TelemetryFile::TSTAMP_ENABLE                },
      {"value",                         TelemetryFile::VALUE                        },
      {"composing_des",                 TelemetryFile::COMPOSING_DES                },
  };

  auto it = lookup.find(name);
  if (it != lookup.end()) {
    return it->second;
  }
  return TelemetryFile::UNKNOWN;
}

SCMITelemetryTarget::SCMITelemetryTarget(SCMITelemetryTargetConfig config)
    : all_des_enable_(config.all_des_enable),
      all_des_tstamp_enable_(config.all_des_tstamp_enable),
      intervals_(std::move(config.intervals)),
      tlm_enable_(config.tlm_enable),
      version_(std::move(config.version)),
      de_implementation_version_(std::move(config.de_implementation_version)),
      data_events_(std::move(config.data_events)) {
  for (const auto& event : data_events_) {
    if (!event->group_id_.has_value()) {
      continue;
    }

    const group_id_t gid = *event->group_id_;
    auto [it, inserted]  = groups_.try_emplace(gid, nullptr);
    if (inserted || it->second == nullptr) {
      auto group = std::make_unique<DesGroup>(gid, all_des_enable_, all_des_tstamp_enable_, intervals_);
      it->second = std::move(group);
    }
    it->second->des_.push_back(event->id_);
  }
}

auto SCMITelemetryTarget::Instance(std::string const& tlm_id) -> SCMITelemetryTarget& {
  auto& instances = Instances();

  // find should never fail. if it does, something else went wrong so crash
  auto it = instances.find(tlm_id);
  return *(it->second);
}

auto SCMITelemetryTarget::GetDataEventById(data_event_id_t identifier) -> DataEvent* {
  auto       id_matches = [identifier](const auto& event) { return event->id_ == identifier; };
  const auto it         = std::find_if(data_events_.begin(), data_events_.end(), id_matches);
  return (it != data_events_.end()) ? it->get() : nullptr;
}

auto HandleProtocolTelemetryWrite(const FileSystemNode* node, const std::string& value) -> ErrorCode {
  return detail::HandleTelemetryWrite(node, value);
}

auto HandleProtocolTelemetryRead(const FileSystemNode* node) -> std::string {
  return detail::HandleTelemetryRead(node);
}

}  // namespace mock_scmi
