// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>

#include "protocol_scmi_ioctl.hpp"
#include "protocol_telemetry.hpp"
#include "scmi_ioctl_uapi.hpp"

namespace {

struct ExtendedAbiInfo {
  scmi_tlm_abi_info       info{};
  std::array<uint8_t, 16> tail{};
};

void Check(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void CheckAbiInfo(const std::string& target) {
  ExtendedAbiInfo request{};
  request.info.size = sizeof(request);
  const auto query  = [&] {
    return mock_scmi::HandleProtocolScmiIoctl(target, SCMI_TLM_GET_ABI_INFO, &request, sizeof(request));
  };

  const auto valid = query();
  Check(valid.error_code == 0 && valid.output.size() == sizeof(request), "zero-filled extension was rejected");
  ExtendedAbiInfo response{};
  std::memcpy(&response, valid.output.data(), sizeof(response));
  Check(response.info.size == sizeof(scmi_tlm_abi_info) && response.info.abi_version == SCMI_TLM_ABI_VERSION_V1,
        "wrong ABI version or driver structure size");
  Check(response.tail == request.tail, "zero-filled extension was not preserved");

  for (auto& byte : request.tail) {
    byte = 1;
    Check(query().error_code == E2BIG, "nonzero extension must return E2BIG");
    byte = 0;
  }
  Check(query().error_code == 0, "rejected request affected subsequent ABI queries");

  request.info.size = sizeof(uint32_t);
  Check(query().error_code == EINVAL, "undersized ABI request must return EINVAL");
  Check(mock_scmi::HandleProtocolScmiIoctl(target, SCMI_TLM_GET_ABI_INFO, nullptr, 0).error_code == EFAULT,
        "missing input must return EFAULT");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ioctl_abi_info_test <telemetry.json>\n";
    return 1;
  }
  try {
    const std::span<char*> arguments{argv, static_cast<size_t>(argc)};
    const auto             targets = mock_scmi::InitScmiTelemetryTargets(std::string{arguments[1]});
    Check(!targets.empty(), "telemetry fixture has no targets");
    CheckAbiInfo(targets.front());
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
