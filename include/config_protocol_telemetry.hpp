// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_CONFIG_PROTOCOL_TELEMETRY_HPP
#define INCLUDE_CONFIG_PROTOCOL_TELEMETRY_HPP

#include <cstdint>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "common_data_generator.hpp"
#include "protocol_telemetry.hpp"

using json = nlohmann::json;

namespace mock_scmi {

/**
 * @brief Default driver settings for SCMI Kernel Telemetry userspace API
 * @see Linux Kernel SCMI Telemetry Support Confluence page:
 *      https://confluence.arm.com/display/CESW/Linux+Kernel+SCMI+Telemetry+Support+-+v4.0+ALPHA_0+--+WIP
 */

class RandomDataEvent : public DataEvent {
 public:
  RandomDataEvent(DataEventConfig config, uint64_t rand_min, uint64_t rand_max)
      : DataEvent(std::move(config)), rand_min_(rand_min), rand_max_(rand_max) {}

  astl_value_t Generate() override {
    static std::random_device               rdev;
    static std::mt19937                     gen(rdev());
    std::uniform_int_distribution<uint64_t> dis(rand_min_, rand_max_);
    uint64_t                                value = dis(gen);
    // TODO(ASTL-116) - Handle all other _astl_value_type_t
    last_value_.ui64 = value;  // NOLINT
    last_timestamp_  = std::chrono::system_clock::now();
    return last_value_;
  }

 private:
  uint64_t rand_min_;
  uint64_t rand_max_;
};

class CsvDataEvent : public DataEvent {
 public:
  CsvDataEvent(DataEventConfig config, const std::string& csv_path, uint8_t column)
      : DataEvent(std::move(config)), csv_gen_(csv_path, column) {}

  astl_value_t Generate() override {
    auto str = csv_gen_.GenerateCSV();
    if (!str.empty()) {
      last_value_.ui64 = std::stoull(str);
      last_timestamp_  = std::chrono::system_clock::now();
    }
    return last_value_;
  }

 private:
  CSVDataGenerator csv_gen_;
};

inline std::vector<std::unique_ptr<DataEvent>> CreateTelemetryDataEvents(std::string const&    json_path,
                                                                         UpdateInterval const& interval) {
  std::vector<std::unique_ptr<DataEvent>> events;

  std::ifstream data_events_json_file(json_path);
  if (!data_events_json_file) {
    throw std::runtime_error("Could not open data events file: " + json_path);
  }
  const json data_events_json = json::parse(data_events_json_file);

  std::cout << "de schema version: " << data_events_json.at("schema_version") << "\n";

  for (const auto& data_event : data_events_json.at("data_events")) {
    const auto data_event_id = static_cast<data_event_id_t>(
        std::stoul(data_event["data_event_id"].get<std::string>(), nullptr, 16));  // NOLINT
    const auto              enable            = static_cast<bool>(data_event["enable"]);
    const auto              tstamp_enable     = static_cast<bool>(data_event["tstamp_enable"]);
    const auto              last_value        = static_cast<astl_value_t>(data_event["last_value"]);  // NOLINT
    const astl_value_type_t value_type        = ToValueType(data_event.at("value_type").get<std::string>());
    const auto              last_timestamp    = std::chrono::system_clock::now();
    const auto              compo_instance_id = static_cast<uint32_t>(data_event["compo_instance_id"]);
    const auto              compo_type        = static_cast<uint32_t>(data_event["compo_type"]);
    const auto              instance_id       = static_cast<uint32_t>(data_event["instance_id"]);
    const auto              persistent        = static_cast<bool>(data_event["persistent"]);
    const auto              tstamp_rate       = static_cast<uint32_t>(data_event["tstamp_rate"]);
    const auto              type              = static_cast<uint32_t>(data_event["type"]);
    const auto              unit              = static_cast<std::string>(data_event["unit"]);
    const auto              unit_exp          = static_cast<std::string>(data_event["unit_exp"]);

    std::optional<group_id_t> group_id = std::nullopt;
    if (data_event.contains("group_id")) {
      group_id = static_cast<group_id_t>(data_event.at("group_id").get<group_id_t>());
    }

    DataEventConfig event_config{
        data_event_id, enable,     tstamp_enable, last_value, value_type, last_timestamp, compo_instance_id, compo_type,
        instance_id,   persistent, tstamp_rate,   type,       unit,       unit_exp,       group_id};

    const auto& gen     = data_event.at("generator");
    const int   type_id = gen.at("type_id").get<int>();
    const auto& params  = gen.at("params");

    switch (type_id) {
      case 1: {  // RandomDataEvent
        const uint64_t rand_min = params.at("min").get<uint64_t>();
        const uint64_t rand_max = params.at("max").get<uint64_t>();
        events.push_back(std::make_unique<RandomDataEvent>(std::move(event_config), rand_min, rand_max));
        break;
      }
      case 2: {  // CsvDataEvent
        const std::string file   = params.at("file_path").get<std::string>();
        const uint32_t    column = params.at("column").get<uint32_t>();

        std::filesystem::path csv_file_path = std::filesystem::path(json_path).parent_path() / file;

        events.push_back(std::make_unique<CsvDataEvent>(std::move(event_config), csv_file_path, column));
        break;
      }
      default:
        throw std::runtime_error("Unknown generator type_id: " + std::to_string(type_id));
    }
  }
  return events;
}

}  // namespace mock_scmi

#endif  // INCLUDE_CONFIG_PROTOCOL_TELEMETRY_HPP
