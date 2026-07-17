// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_SCMI_BACKEND_SELECTION_HPP
#define INCLUDE_SCMI_BACKEND_SELECTION_HPP

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace mock_scmi {

constexpr const char* kScmiInterfaceEnvVar = "ASTL_SCMI_INTERFACE";

enum class ScmiBackendPreference {
  AUTO,
  IOCTL,
  SYSFS,
};

inline auto GetScmiBackendPreference() -> ScmiBackendPreference {
  auto        preference = ScmiBackendPreference::AUTO;
  const char* raw_value  = std::getenv(kScmiInterfaceEnvVar);
  if (raw_value != nullptr) {
    std::string value{raw_value};
    std::ranges::transform(value, value.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });

    if (value == "ioctl") {
      preference = ScmiBackendPreference::IOCTL;
    } else if (value == "sysfs") {
      preference = ScmiBackendPreference::SYSFS;
    }
  }
  return preference;
}

inline auto BackendPreferenceName(ScmiBackendPreference preference) -> const char* {
  switch (preference) {
    case ScmiBackendPreference::AUTO:
      return "auto";
    case ScmiBackendPreference::IOCTL:
      return "ioctl";
    case ScmiBackendPreference::SYSFS:
      return "sysfs";
  }
  return "auto";
}

}  // namespace mock_scmi

#endif  // INCLUDE_SCMI_BACKEND_SELECTION_HPP
