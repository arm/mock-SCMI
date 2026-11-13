// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <cuse_lowlevel.h>
#include <fuse_lowlevel.h>
#include <sys/ioctl.h>
#include <sys/uio.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "protocol_scmi_ioctl.hpp"
#include "protocol_telemetry.hpp"
#include "scmi_ioctl_uapi.hpp"

namespace {

constexpr std::string_view kReadyMarker = "eccf4f7c-d1b1-47f0-9d23-159f6d38b661";

struct Options {
  std::string config_path;
  std::string target_id;
  std::string device_name;
  bool        list_targets{false};
};

struct CuseDevice {
  std::string target_id;
};

auto PrintUsage(const char* executable) -> void {
  std::cerr << "usage:\n"
            << "  " << executable << " --list-targets --tlm-json-path FILE\n"
            << "  " << executable << " --target ID --device-name NAME --tlm-json-path FILE\n";
}

auto ParseOptions(std::span<char*> arguments) -> std::optional<Options> {
  Options options;
  for (size_t index = 1; index < arguments.size(); ++index) {
    const std::string argument{arguments[index]};
    if (argument == "--list-targets") {
      options.list_targets = true;
      continue;
    }
    if (index + 1 >= arguments.size()) {
      return std::nullopt;
    }
    const std::string value{arguments[++index]};
    if (argument == "--tlm-json-path") {
      options.config_path = value;
    } else if (argument == "--target") {
      options.target_id = value;
    } else if (argument == "--device-name") {
      options.device_name = value;
    } else {
      return std::nullopt;
    }
  }

  if (options.config_path.empty()) {
    return std::nullopt;
  }
  if (!options.list_targets && (options.target_id.empty() || options.device_name.empty())) {
    return std::nullopt;
  }
  return options;
}

auto ListTargets(const std::string& config_path) -> int {
  std::ifstream input{config_path};
  if (!input) {
    std::cerr << "Could not open telemetry configuration: " << config_path << '\n';
    return 1;
  }

  try {
    const auto config = nlohmann::json::parse(input);
    for (const auto& target : config.at("targets")) {
      std::cout << target.at("instance").get<std::string>() << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "Could not parse telemetry configuration: " << error.what() << '\n';
    return 1;
  }
  return 0;
}

auto ReplyWithProtocolResult(fuse_req_t request, const CuseDevice& device, unsigned int command, const void* input,
                             size_t input_size, size_t output_size) -> void {
  auto reply = mock_scmi::HandleProtocolScmiIoctl(device.target_id, command, input, input_size);
  if (reply.error_code != 0) {
    fuse_reply_err(request, reply.error_code);
    return;
  }
  const auto reply_size = std::min(reply.output.size(), output_size);
  fuse_reply_ioctl(request, 0, reply.output.data(), reply_size);
}

auto RequestTopLevelArgument(fuse_req_t request, unsigned int command, void* argument) -> bool {
  const size_t size = _IOC_SIZE(command);
  if (size == 0) {
    return false;
  }

  iovec        input_iov{.iov_base = argument, .iov_len = size};
  iovec        output_iov{.iov_base = argument, .iov_len = size};
  const iovec* input  = (_IOC_DIR(command) & _IOC_WRITE) != 0 ? &input_iov : nullptr;
  const iovec* output = (_IOC_DIR(command) & _IOC_READ) != 0 ? &output_iov : nullptr;
  fuse_reply_ioctl_retry(request, input, input != nullptr ? 1 : 0, output, output != nullptr ? 1 : 0);
  return true;
}

auto RequestAbiInfoExtension(fuse_req_t request, unsigned int command, void* argument, const void* input,
                             size_t input_size) -> bool {
  if (command != SCMI_TLM_GET_ABI_INFO || input_size < sizeof(uint32_t)) {
    return false;
  }

  uint32_t requested_size{};
  std::memcpy(&requested_size, input, sizeof(requested_size));
  if (requested_size <= input_size) {
    return false;
  }

  // ABI V1 uses the caller's size to validate and zero any future extension bytes.
  // The initial ioctl retry only copies the fixed-size structure encoded in the command.
  const iovec buffer{.iov_base = argument, .iov_len = requested_size};
  fuse_reply_ioctl_retry(request, &buffer, 1, &buffer, 1);
  return true;
}

auto RequestBatchPointers(fuse_req_t request, unsigned int command, void* argument, const void* input,
                          size_t input_size) -> bool {
  if ((command != SCMI_TLM_GET_DE_CFG && command != SCMI_TLM_SET_DE_CFG) || input_size != sizeof(scmi_tlm_batch)) {
    return false;
  }

  scmi_tlm_batch batch{};
  std::memcpy(&batch, input, sizeof(batch));
  if (batch.num_items == 0 || batch.item_sz != sizeof(scmi_tlm_de_config) ||
      batch.num_items > std::numeric_limits<size_t>::max() / batch.item_sz) {
    fuse_reply_err(request, EINVAL);
    return true;
  }

  const size_t         items_size  = static_cast<size_t>(batch.num_items) * batch.item_sz;
  const size_t         states_size = static_cast<size_t>(batch.num_items) * sizeof(int32_t);
  std::array<iovec, 3> input_iov{};
  std::array<iovec, 3> output_iov{};
  input_iov[0]     = iovec{.iov_base = argument, .iov_len = sizeof(batch)};
  output_iov[0]    = input_iov[0];
  input_iov[1]     = iovec{.iov_base = reinterpret_cast<void*>(batch.items), .iov_len = items_size};  // NOLINT
  output_iov[1]    = input_iov[1];
  size_t iov_count = 2;
  if (batch.states != 0) {
    input_iov[2]  = iovec{.iov_base = reinterpret_cast<void*>(batch.states), .iov_len = states_size};  // NOLINT
    output_iov[2] = input_iov[2];
    ++iov_count;
  }
  fuse_reply_ioctl_retry(request, input_iov.data(), iov_count, output_iov.data(), iov_count);
  return true;
}

auto CuseOpen(fuse_req_t request, fuse_file_info* file_info) -> void { fuse_reply_open(request, file_info); }

auto CuseRead(fuse_req_t request, size_t /*size*/, off_t /*offset*/, fuse_file_info* /*file_info*/) -> void {
  fuse_reply_buf(request, nullptr, 0);
}

auto CuseWrite(fuse_req_t request, const char* /*buffer*/, size_t size, off_t /*offset*/, fuse_file_info* /*file_info*/)
    -> void {
  fuse_reply_write(request, size);
}

auto CuseIoctl(fuse_req_t request, int command, void* argument, fuse_file_info* /*file_info*/, unsigned int flags,
               const void* input, size_t input_size, size_t output_size) -> void {
  if ((flags & FUSE_IOCTL_COMPAT) != 0) {
    fuse_reply_err(request, ENOSYS);
    return;
  }

  if (input_size == 0 && output_size == 0 &&
      RequestTopLevelArgument(request, static_cast<unsigned int>(command), argument)) {
    return;
  }
  if (RequestAbiInfoExtension(request, static_cast<unsigned int>(command), argument, input, input_size) ||
      RequestBatchPointers(request, static_cast<unsigned int>(command), argument, input, input_size)) {
    return;
  }

  const auto* device = static_cast<const CuseDevice*>(fuse_req_userdata(request));
  ReplyWithProtocolResult(request, *device, static_cast<unsigned int>(command), input, input_size, output_size);
}

const cuse_lowlevel_ops kCuseOperations{
    .init      = nullptr,
    .init_done = nullptr,
    .destroy   = nullptr,
    .open      = CuseOpen,
    .read      = CuseRead,
    .write     = CuseWrite,
    .flush     = nullptr,
    .release   = nullptr,
    .fsync     = nullptr,
    .ioctl     = CuseIoctl,
    .poll      = nullptr,
};

auto RunCuseDevice(const Options& options) -> int {
  const auto target_ids = mock_scmi::InitScmiTelemetryTargets(options.config_path);
  if (std::ranges::find(target_ids, options.target_id) == target_ids.end()) {
    std::cerr << "Telemetry target not found in configuration: " << options.target_id << '\n';
    return 1;
  }

  CuseDevice                 device{.target_id = options.target_id};
  std::string                devname = "DEVNAME=" + options.device_name;
  std::array<const char*, 1> device_info{devname.c_str()};
  cuse_info                  cuse_config{
                       .dev_major     = 0,
                       .dev_minor     = 0,
                       .dev_info_argc = 1,
                       .dev_info_argv = device_info.data(),
                       .flags         = CUSE_UNRESTRICTED_IOCTL,
  };

  std::cout << "Mock CUSE device initialized! target=" << options.target_id << " device=/dev/" << options.device_name
            << " - " << kReadyMarker << std::endl;
  std::string          program_name{"MockScmiCuse"};
  std::string          foreground{"-f"};
  std::string          single_threaded{"-s"};
  std::array<char*, 3> cuse_argv{program_name.data(), foreground.data(), single_threaded.data()};
  return cuse_lowlevel_main(static_cast<int>(cuse_argv.size()), cuse_argv.data(), &cuse_config, &kCuseOperations,
                            &device);
}

}  // namespace

int main(int argc, char** argv) {
  const auto arguments = std::span{argv, static_cast<size_t>(argc)};
  const auto options   = ParseOptions(arguments);
  if (!options.has_value()) {
    PrintUsage(arguments.front());
    return 2;
  }
  return options->list_targets ? ListTargets(options->config_path) : RunCuseDevice(*options);
}
