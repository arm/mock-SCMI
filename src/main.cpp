// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
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
#include "mock_scmi.hpp"  // Declares: extern const fuse_lowlevel_ops fuse_ll_ops;

namespace {

struct MockScmiOptions {
  char* tlm_json_path = nullptr;
};

const std::array<fuse_opt, 4> kMockScmiOptionSpec{
    {
     {"-t %s", offsetof(MockScmiOptions, tlm_json_path), 0},
     {"--tlm-json-path %s", offsetof(MockScmiOptions, tlm_json_path), 0},
     {"--tlm-json-path=%s", offsetof(MockScmiOptions, tlm_json_path), 0},
     FUSE_OPT_END, }
};

auto ToTlmJsonPath(const MockScmiOptions& options) -> std::optional<std::string> {
  if (options.tlm_json_path == nullptr) {
    return std::nullopt;
  }

  std::string tlm_json_path{options.tlm_json_path};
  if (!tlm_json_path.empty() && tlm_json_path.front() == '=') {
    tlm_json_path.erase(0, 1);
  }

  return tlm_json_path;
}

auto PrintMockScmiHelp() -> void {
  std::cout << "    -t FILE\n";
  std::cout << "    --tlm-json-path FILE  path to the MockScmi telemetry JSON configuration\n";
  std::cout << "    MOCK_SCMI_INTERFACE selects backend: sysfs/auto for the FUSE tree, ioctl for tlm_N devices\n";
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

namespace {

struct MockScmiRuntime {
  MockScmiRuntime(int argc, char** argv) : args{argc, argv, 0} {}

  MockScmiRuntime(const MockScmiRuntime&)            = delete;
  MockScmiRuntime& operator=(const MockScmiRuntime&) = delete;
  MockScmiRuntime(MockScmiRuntime&&)                 = delete;
  MockScmiRuntime& operator=(MockScmiRuntime&&)      = delete;

  ~MockScmiRuntime() {
    if (mounted && session != nullptr) {
      fuse_session_unmount(session);
    }
    CleanFuse(&args, session, loop_config);
  }

  fuse_args                                   args;
  fuse_cmdline_opts                           options{};
  MockScmiOptions                             mock_scmi_options{};
  fuse_session*                               session{nullptr};
  fuse_loop_config*                           loop_config{nullptr};
  bool                                        mounted{false};
  mock_scmi::FuseUserData                     user_data{};
  std::unique_ptr<char, decltype(&std::free)> tlm_json_path{nullptr, std::free};
  std::unique_ptr<char, decltype(&std::free)> mountpoint{nullptr, std::free};
};

auto ParseOptions(MockScmiRuntime& runtime) -> std::optional<int> {
  std::optional<int> result;
  const int          parse_result =
      fuse_opt_parse(&runtime.args, &runtime.mock_scmi_options, kMockScmiOptionSpec.data(), nullptr);
  runtime.tlm_json_path.reset(runtime.mock_scmi_options.tlm_json_path);

  if (parse_result != 0) {
    result = static_cast<int>(mock_scmi::ErrorCode::CMDLINE_PARSE_ERROR);
  } else if (fuse_parse_cmdline(&runtime.args, &runtime.options) != 0) {
    runtime.mountpoint.reset(runtime.options.mountpoint);
    result = static_cast<int>(mock_scmi::ErrorCode::CMDLINE_PARSE_ERROR);
  } else {
    runtime.mountpoint.reset(runtime.options.mountpoint);
    if (runtime.options.show_help) {
      std::cout << "usage: ./MockScmi [options] <mountpoint>\n\n";
      PrintMockScmiHelp();
      fuse_cmdline_help();
      fuse_lowlevel_help();
      result = static_cast<int>(mock_scmi::ErrorCode::SUCCESS);
    } else if (runtime.options.show_version) {
      std::cout << "FUSE library version " << fuse_pkgversion() << "\n";
      fuse_lowlevel_version();
      result = static_cast<int>(mock_scmi::ErrorCode::SUCCESS);
    } else if (runtime.options.mountpoint == nullptr) {
      std::cout << "usage: ./MockScmi [options] <mountpoint>\n";
      std::cout << "       ./MockScmi --help\n";
      result = static_cast<int>(mock_scmi::ErrorCode::MOUNTPOINT_MISSING);
    }
  }

  return result;
}

auto RunFuseLoop(MockScmiRuntime& runtime) -> int {
  if (runtime.options.singlethread) {
    return fuse_session_loop(runtime.session);
  }

  runtime.loop_config = fuse_loop_cfg_create();
  fuse_loop_cfg_set_clone_fd(runtime.loop_config, runtime.options.clone_fd);
  fuse_loop_cfg_set_max_threads(runtime.loop_config, runtime.options.max_threads);
  return fuse_session_loop_mt(runtime.session, runtime.loop_config);
}

auto RunSession(MockScmiRuntime& runtime) -> int {
  runtime.user_data.tlm_json_path           = ToTlmJsonPath(runtime.mock_scmi_options);
  runtime.user_data.scmi_backend_preference = mock_scmi::GetScmiBackendPreference();
  runtime.session = fuse_session_new(&runtime.args, &mock_scmi::kFuseLowLevelOps, sizeof(mock_scmi::kFuseLowLevelOps),
                                     &runtime.user_data);

  if (runtime.session == nullptr) {
    return static_cast<int>(mock_scmi::ErrorCode::SESSION_CREATION_FAILED);
  }
  if (fuse_set_signal_handlers(runtime.session) != 0) {
    return static_cast<int>(mock_scmi::ErrorCode::SIGNAL_HANDLER_ERROR);
  }
  if (fuse_session_mount(runtime.session, runtime.options.mountpoint) != 0) {
    return static_cast<int>(mock_scmi::ErrorCode::SYSFS_MOUNT_FAILED);
  }

  runtime.mounted = true;
  fuse_daemonize(runtime.options.foreground);
  return RunFuseLoop(runtime) == 0 ? static_cast<int>(mock_scmi::ErrorCode::SUCCESS)
                                   : static_cast<int>(mock_scmi::ErrorCode::SESSION_LOOP_FAILED);
}

}  // namespace

int main(int argc, char** argv) {
  MockScmiRuntime runtime(argc, argv);
  const auto      option_result = ParseOptions(runtime);
  return option_result.has_value() ? *option_result : RunSession(runtime);
}
