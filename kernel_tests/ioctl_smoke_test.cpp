// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <string>

#include "scmi_ioctl_uapi.hpp"

namespace {

constexpr uint32_t kSmokeTestDataEventId    = 0;
constexpr uint32_t kSecondTargetDataEventId = 0x00010000;
constexpr uint32_t kWattsDataEventId        = 0x00009E4F;
constexpr uint32_t kMegahertzDataEventId    = 0x00001A68;
constexpr uint32_t kMissingDataEventId      = 0xDEADBEEF;
constexpr uint32_t kMissingGroupId          = 0xFFFF;
constexpr uint64_t kBadSizeGetDeConfig      = _IOC(_IOC_READ | _IOC_WRITE, SCMI_IOCTL_MAGIC, 0x04, sizeof(uint32_t));

auto FormatDeImplementationVersion(const scmi_tlm_base_info& info) -> std::string {
  return std::format("{:08X}{:08X}{:08X}{:08X}", info.de_impl_version[0], info.de_impl_version[1],
                     info.de_impl_version[2], info.de_impl_version[3]);
}

auto OpenDevice(const std::filesystem::path& device_path) -> int {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  return open(device_path.c_str(), O_RDWR | O_CLOEXEC);
}

auto RunIoctl(int file_descriptor, uint64_t request, void* arg, const char* name) -> bool {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,google-runtime-int)
  if (ioctl(file_descriptor, static_cast<unsigned long>(request), arg) != 0) {
    std::cerr << name << " failed: " << std::strerror(errno) << '\n';
    return false;
  }
  return true;
}

auto RunIoctlExpectErr(int file_descriptor, uint64_t request, void* arg, const char* name, int expected_errno) -> bool {
  errno = 0;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,google-runtime-int)
  if (ioctl(file_descriptor, static_cast<unsigned long>(request), arg) == 0) {
    std::cerr << name << " unexpectedly succeeded\n";
    return false;
  }
  if (errno != expected_errno) {
    std::cerr << name << " failed with errno=" << errno << ", expected=" << expected_errno << '\n';
    return false;
  }
  std::cout << name << " errno=" << errno << '\n';
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ioctl_smoke_test <device>\n";
    return 1;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const std::filesystem::path device_path{argv[1]};
  int                         file_descriptor = OpenDevice(device_path);
  if (file_descriptor < 0) {
    std::cerr << "open failed: " << std::strerror(errno) << '\n';
    return 1;
  }

  scmi_tlm_base_info info{};
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_INFO, &info, "SCMI_TLM_GET_INFO")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("info version=0x{:08X} de_impl={} num_des={} num_groups={} num_intervals={} num_shmtis={}\n",
                           info.version, FormatDeImplementationVersion(info), info.num_des, info.num_groups,
                           info.num_intervals, info.num_shmtis);

  scmi_tlm_config target_config{};
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_CFG, &target_config, "SCMI_TLM_GET_CFG")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("cfg enable={} t_enable={} active={},{}\n", static_cast<int>(target_config.enable),
                           static_cast<int>(target_config.t_enable), target_config.active.secs,
                           target_config.active.exp);

  target_config.enable = 0;
  if (!RunIoctl(file_descriptor, SCMI_TLM_SET_CFG, &target_config, "SCMI_TLM_SET_CFG disable")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("cfg_after_disable enable={} t_enable={}\n", static_cast<int>(target_config.enable),
                           static_cast<int>(target_config.t_enable));

  scmi_tlm_de_config de_config{};
  de_config.id = kSmokeTestDataEventId;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_DE_CFG, &de_config, "SCMI_TLM_GET_DE_CFG")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("de_cfg_before id=0x{:08X} enable={} t_enable={}\n", de_config.id, de_config.enable,
                           de_config.t_enable);

  de_config.enable = 1;
  if (!RunIoctl(file_descriptor, SCMI_TLM_SET_DE_CFG, &de_config, "SCMI_TLM_SET_DE_CFG")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("de_cfg_after id=0x{:08X} enable={} t_enable={}\n", de_config.id, de_config.enable,
                           de_config.t_enable);

  scmi_tlm_de_sample disabled_sample{};
  disabled_sample.id = kSmokeTestDataEventId;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_DE_VALUE, &disabled_sample, "SCMI_TLM_GET_DE_VALUE disabled")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("sample_disabled id=0x{:08X} tstamp={} val={}\n", disabled_sample.id, disabled_sample.tstamp,
                           disabled_sample.val);

  target_config.enable = 1;
  if (!RunIoctl(file_descriptor, SCMI_TLM_SET_CFG, &target_config, "SCMI_TLM_SET_CFG enable")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("cfg_after_enable enable={} t_enable={}\n", static_cast<int>(target_config.enable),
                           static_cast<int>(target_config.t_enable));

  scmi_tlm_de_info de_info{};
  de_info.id = kSmokeTestDataEventId;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_DE_INFO, &de_info, "SCMI_TLM_GET_DE_INFO")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("de_info id=0x{:08X} ts_rate={} data_sz={} persistent={}\n", de_info.id, de_info.ts_rate,
                           de_info.data_sz, static_cast<int>(de_info.persistent));

  scmi_tlm_de_info watts_info{};
  watts_info.id = kWattsDataEventId;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_DE_INFO, &watts_info, "SCMI_TLM_GET_DE_INFO watts")) {
    close(file_descriptor);
    return 1;
  }

  scmi_tlm_de_info megahertz_info{};
  megahertz_info.id = kMegahertzDataEventId;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_DE_INFO, &megahertz_info, "SCMI_TLM_GET_DE_INFO megahertz")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("de_units watts={} megahertz={}\n", watts_info.unit, megahertz_info.unit);

  scmi_tlm_de_sample sample{};
  sample.id = kSmokeTestDataEventId;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_DE_VALUE, &sample, "SCMI_TLM_GET_DE_VALUE")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("sample id=0x{:08X} read=ok\n", sample.id);

  scmi_tlm_de_config all_de_config{};
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_ALL_CFG, &all_de_config, "SCMI_TLM_GET_ALL_CFG")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("all_de_cfg_before enable={} t_enable={}\n", all_de_config.enable, all_de_config.t_enable);

  all_de_config.enable   = 0;
  all_de_config.t_enable = 0;
  if (!RunIoctl(file_descriptor, SCMI_TLM_SET_ALL_CFG, &all_de_config, "SCMI_TLM_SET_ALL_CFG")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("all_de_cfg_after enable={} t_enable={}\n", all_de_config.enable, all_de_config.t_enable);

  scmi_tlm_de_config disabled_de_config{};
  disabled_de_config.id = kSmokeTestDataEventId;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_DE_CFG, &disabled_de_config, "SCMI_TLM_GET_DE_CFG after all")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("de_cfg_after_all id=0x{:08X} enable={} t_enable={}\n", disabled_de_config.id,
                           disabled_de_config.enable, disabled_de_config.t_enable);

  scmi_tlm_grp_info group_info{};
  group_info.grp_id = 0;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_GRP_INFO, &group_info, "SCMI_TLM_GET_GRP_INFO group0")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("group_info id={} num_des={} num_intervals={}\n", group_info.grp_id, group_info.num_des,
                           group_info.num_intervals);

  group_info.grp_id = 1;
  if (!RunIoctl(file_descriptor, SCMI_TLM_GET_GRP_INFO, &group_info, "SCMI_TLM_GET_GRP_INFO group1")) {
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("group_info id={} num_des={} num_intervals={}\n", group_info.grp_id, group_info.num_des,
                           group_info.num_intervals);

  scmi_tlm_de_config missing_de_config{};
  missing_de_config.id = kMissingDataEventId;
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_GET_DE_CFG, &missing_de_config, "missing_de_cfg", EINVAL)) {
    close(file_descriptor);
    return 1;
  }

  group_info.grp_id = kMissingGroupId;
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_GET_GRP_INFO, &group_info, "missing_group_info", EINVAL)) {
    close(file_descriptor);
    return 1;
  }

  uint32_t bad_size_arg{};
  if (!RunIoctlExpectErr(file_descriptor, kBadSizeGetDeConfig, &bad_size_arg, "bad_size_get_de_cfg", ENOTTY)) {
    close(file_descriptor);
    return 1;
  }

  scmi_tlm_intervals intervals{};
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_GET_INTRVS, &intervals, "unsupported_get_intervals", ENOTTY)) {
    close(file_descriptor);
    return 1;
  }

  scmi_tlm_des_list des_list{};
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_GET_DE_LIST, &des_list, "unsupported_get_de_list", ENOTTY)) {
    close(file_descriptor);
    return 1;
  }

  scmi_tlm_grps_list groups_list{};
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_GET_GRP_LIST, &groups_list, "unsupported_get_group_list", ENOTTY)) {
    close(file_descriptor);
    return 1;
  }

  scmi_tlm_grp_desc group_desc{};
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_GET_GRP_DESC, &group_desc, "unsupported_get_group_desc", ENOTTY)) {
    close(file_descriptor);
    return 1;
  }

  scmi_tlm_data_read data_read{};
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_SINGLE_SAMPLE, &data_read, "unsupported_single_sample", ENOTTY)) {
    close(file_descriptor);
    return 1;
  }
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_BULK_READ, &data_read, "unsupported_bulk_read", ENOTTY)) {
    close(file_descriptor);
    return 1;
  }
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_BATCH_READ, &data_read, "unsupported_batch_read", ENOTTY)) {
    close(file_descriptor);
    return 1;
  }

  scmi_tlm_shmtis_list shmtis_list{};
  if (!RunIoctlExpectErr(file_descriptor, SCMI_TLM_GET_SHMTI_LIST, &shmtis_list, "unsupported_get_shmti_list",
                         ENOTTY)) {
    close(file_descriptor);
    return 1;
  }

  const auto second_device          = device_path.parent_path() / "tlm_1";
  int        second_file_descriptor = OpenDevice(second_device);
  if (second_file_descriptor < 0) {
    std::cerr << "open tlm_1 failed: " << std::strerror(errno) << '\n';
    close(file_descriptor);
    return 1;
  }

  scmi_tlm_base_info second_info{};
  if (!RunIoctl(second_file_descriptor, SCMI_TLM_GET_INFO, &second_info, "SCMI_TLM_GET_INFO tlm_1")) {
    close(second_file_descriptor);
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("info_tlm_1 de_impl={}\n", FormatDeImplementationVersion(second_info));

  scmi_tlm_de_config second_de_config{};
  second_de_config.id = kSecondTargetDataEventId;
  if (!RunIoctl(second_file_descriptor, SCMI_TLM_GET_DE_CFG, &second_de_config, "SCMI_TLM_GET_DE_CFG tlm_1")) {
    close(second_file_descriptor);
    close(file_descriptor);
    return 1;
  }
  std::cout << std::format("de_cfg_tlm_1 id=0x{:08X} enable={} t_enable={}\n", second_de_config.id,
                           second_de_config.enable, second_de_config.t_enable);

  close(second_file_descriptor);
  close(file_descriptor);
  return 0;
}
