// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include <fuse_lowlevel.h>
#include <fuse_opt.h>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>

#include "common.hpp"
#include "mock_sysfs.hpp"  // Declares: extern const fuse_lowlevel_ops fuse_ll_ops;

namespace {

struct MockSysfsOptions {
  char* tlm_json_path = nullptr;
};

const std::array<fuse_opt, 4> kMockSysfsOptionSpec{
    {
     {"-t %s", offsetof(MockSysfsOptions, tlm_json_path), 0},
     {"--tlm-json-path %s", offsetof(MockSysfsOptions, tlm_json_path), 0},
     {"--tlm-json-path=%s", offsetof(MockSysfsOptions, tlm_json_path), 0},
     FUSE_OPT_END, }
};

auto ToTlmJsonPath(const MockSysfsOptions& options) -> std::optional<std::string> {
  if (options.tlm_json_path == nullptr) {
    return std::nullopt;
  }

  std::string tlm_json_path{options.tlm_json_path};
  if (!tlm_json_path.empty() && tlm_json_path.front() == '=') {
    tlm_json_path.erase(0, 1);
  }

  return tlm_json_path;
}

auto PrintMockSysfsHelp() -> void {
  std::cout << "    -t FILE\n";
  std::cout << "    --tlm-json-path FILE  path to the MockSysfs telemetry JSON configuration\n";
}

}  // namespace

void CleanFuse(fuse_args* args, fuse_session* fuse_session_handle, fuse_loop_config* config) {
  if (fuse_session_handle) {
    fuse_remove_signal_handlers(fuse_session_handle);
    fuse_session_destroy(fuse_session_handle);
  }

  fuse_loop_cfg_destroy(config);
  fuse_opt_free_args(args);
}

int main(int argc, char* argv[]) {
  int               ret  = -1;
  fuse_args         args = FUSE_ARGS_INIT(argc, argv);
  fuse_cmdline_opts opts{};
  MockSysfsOptions  mock_sysfs_options{};
  fuse_session*     fuse_session_handle = nullptr;
  fuse_loop_config* config{};

  const int parse_result = fuse_opt_parse(&args, &mock_sysfs_options, kMockSysfsOptionSpec.data(), nullptr);
  std::unique_ptr<char, decltype(&std::free)> tlm_json_path_raii(mock_sysfs_options.tlm_json_path, std::free);
  if (parse_result != 0) {
    CleanFuse(&args, nullptr, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::CMDLINE_PARSE_ERROR);
    return ret;
  }

  if (fuse_parse_cmdline(&args, &opts)) {
    CleanFuse(&args, nullptr, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::CMDLINE_PARSE_ERROR);
    return ret;
  }

  std::unique_ptr<char, decltype(&std::free)> mountpoint_raii(opts.mountpoint, std::free);

  if (opts.show_help) {
    std::cout << "usage: ./MockSysfs [options] <mountpoint>\n\n";
    PrintMockSysfsHelp();
    fuse_cmdline_help();
    fuse_lowlevel_help();
    CleanFuse(&args, nullptr, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SUCCESS);
    return ret;
  }

  if (opts.show_version) {
    std::cout << "FUSE library version " << fuse_pkgversion() << "\n";
    fuse_lowlevel_version();
    CleanFuse(&args, nullptr, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SUCCESS);
    return ret;
  }

  if (opts.mountpoint == nullptr) {
    std::cout << "usage: ./MockSysfs [options] <mountpoint>\n";
    std::cout << "       ./MockSysfs --help\n";
    CleanFuse(&args, nullptr, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::MOUNTPOINT_MISSING);
    return ret;
  }

  mock_sysfs::FuseUserData fuse_user_data{};
  fuse_user_data.tlm_json_path = ToTlmJsonPath(mock_sysfs_options);

  fuse_session_handle =
      fuse_session_new(&args, &mock_sysfs::kFuseLowLevelOps, sizeof(mock_sysfs::kFuseLowLevelOps), &fuse_user_data);
  if (fuse_session_handle == nullptr) {
    CleanFuse(&args, fuse_session_handle, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SESSION_CREATION_FAILED);
    return ret;
  }

  if (fuse_set_signal_handlers(fuse_session_handle) != 0) {
    CleanFuse(&args, fuse_session_handle, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SIGNAL_HANDLER_ERROR);
    return ret;
  }

  if (fuse_session_mount(fuse_session_handle, opts.mountpoint) != 0) {
    CleanFuse(&args, fuse_session_handle, config);
    ret = static_cast<int>(mock_sysfs::ErrorCode::SYSFS_MOUNT_FAILED);
    return ret;
  }

  fuse_daemonize(opts.foreground);

  if (opts.singlethread) {
    ret = fuse_session_loop(fuse_session_handle);
  } else {
    config = fuse_loop_cfg_create();
    fuse_loop_cfg_set_clone_fd(config, opts.clone_fd);
    fuse_loop_cfg_set_max_threads(config, opts.max_threads);
    ret = fuse_session_loop_mt(fuse_session_handle, config);
  }

  fuse_session_unmount(fuse_session_handle);
  CleanFuse(&args, fuse_session_handle, config);

  return ret ? static_cast<int>(mock_sysfs::ErrorCode::SESSION_LOOP_FAILED)
             : static_cast<int>(mock_sysfs::ErrorCode::SUCCESS);
}
