# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.14)
project(
  MockScmi
  VERSION 1.0.0
  LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

if(PROJECT_IS_TOP_LEVEL)
  add_custom_target(
    format
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/format.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting C++, CMake, shell, Markdown, and YAML files")

  add_custom_target(
    check_format
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check_format.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking C++ and CMake formatting")

  add_custom_target(
    lint
    COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/lint.sh ${CMAKE_CURRENT_BINARY_DIR} all
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy and shell lint")
endif()

include(CTest)
include(FetchContent)
include(GNUInstallDirs)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED IMPORTED_TARGET fuse3>=3.12)

# nlohmann/json used to be supplied by the former parent build. Prefer an installed package, but fetch a pinned release
# when this repository is built on its own.
find_package(nlohmann_json 3.11.3 CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

file(GLOB_RECURSE MOCK_SCMI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER MOCK_SCMI_SOURCES EXCLUDE REGEX "/cuse_transport/")

file(GLOB MOCK_SCMI_PROTOCOL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/protocols/*.cpp")

add_executable(MockScmi ${MOCK_SCMI_SOURCES})
add_executable(MockScmiCuse ${CMAKE_CURRENT_SOURCE_DIR}/src/cuse_transport/mock_scmi_cuse.cpp
                            ${CMAKE_CURRENT_SOURCE_DIR}/src/fuse_transport/fsnode.cpp ${MOCK_SCMI_PROTOCOL_SOURCES})

target_compile_definitions(MockScmi PRIVATE FUSE_USE_VERSION=316 # make sure to update scripts/lint.sh if this changes
)

target_compile_features(MockScmi PRIVATE cxx_std_20)
target_include_directories(MockScmi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(MockScmi PRIVATE PkgConfig::FUSE3 nlohmann_json::nlohmann_json)

target_compile_definitions(MockScmiCuse PRIVATE FUSE_USE_VERSION=316)
target_compile_features(MockScmiCuse PRIVATE cxx_std_20)
target_include_directories(MockScmiCuse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(MockScmiCuse PRIVATE PkgConfig::FUSE3 nlohmann_json::nlohmann_json)

install(TARGETS MockScmi MockScmiCuse RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY config/ DESTINATION ${CMAKE_INSTALL_DATADIR}/mock_scmi)

if(BUILD_TESTING)
  add_executable(MockScmiAbiInfoTest kernel_tests/ioctl_abi_info_test.cpp src/fuse_transport/fsnode.cpp
                                     ${MOCK_SCMI_PROTOCOL_SOURCES})
  target_compile_features(MockScmiAbiInfoTest PRIVATE cxx_std_20)
  target_compile_definitions(MockScmiAbiInfoTest PRIVATE FUSE_USE_VERSION=316)
  target_include_directories(MockScmiAbiInfoTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(MockScmiAbiInfoTest PRIVATE PkgConfig::FUSE3 nlohmann_json::nlohmann_json)
  add_test(NAME toolsMockScmiAbiInfo COMMAND MockScmiAbiInfoTest ${CMAKE_CURRENT_SOURCE_DIR}/config/tlm.json)
  set_tests_properties(toolsMockScmiAbiInfo PROPERTIES LABELS "mockscmi")

  add_executable(MockScmiIoctlSmokeTest ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/ioctl_smoke_test.cpp)
  target_compile_features(MockScmiIoctlSmokeTest PRIVATE cxx_std_20)
  target_include_directories(MockScmiIoctlSmokeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  add_test(NAME toolsMockScmiHelp COMMAND MockScmi --help)
  set_tests_properties(toolsMockScmiHelp PROPERTIES LABELS "smoke;mockscmi" PASS_REGULAR_EXPRESSION "tlm-json-path")

  add_test(
    NAME toolsMockScmiFind
    COMMAND
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/kernel_test_fixture.sh $<TARGET_FILE:MockScmi>
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/find/expected.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/find/command.sh)
  set_tests_properties(toolsMockScmiFind PROPERTIES LABELS "backend_sysfs;integration;mockscmi;valgrind_isolated"
                                                    SKIP_RETURN_CODE 77)

  add_test(
    NAME toolsMockScmiCdPermissions
    COMMAND
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/kernel_test_fixture.sh $<TARGET_FILE:MockScmi>
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/cd_permissions/expected.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/cd_permissions/command.sh)
  set_tests_properties(toolsMockScmiCdPermissions
                       PROPERTIES LABELS "backend_sysfs;integration;mockscmi;valgrind_isolated" SKIP_RETURN_CODE 77)

  add_test(
    NAME toolsMockScmiAllDesEnableToIndividualEnableConsistency
    COMMAND
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/kernel_test_fixture.sh $<TARGET_FILE:MockScmi>
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/all_des_enable_to_individual_enable_consistency/expected.txt
      ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/all_des_enable_to_individual_enable_consistency/command.sh)
  set_tests_properties(toolsMockScmiAllDesEnableToIndividualEnableConsistency
                       PROPERTIES LABELS "backend_sysfs;integration;mockscmi;valgrind_isolated" SKIP_RETURN_CODE 77)

  add_test(
    NAME toolsMockScmiIoctlBackend
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/cuse_test_fixture.sh $<TARGET_FILE:MockScmiCuse>
            $<TARGET_FILE:MockScmiIoctlSmokeTest> ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/ioctl_backend/expected.txt)
  set_tests_properties(
    toolsMockScmiIoctlBackend PROPERTIES LABELS "backend_ioctl;integration;mockscmi;valgrind_isolated" SKIP_RETURN_CODE
                                         77 ENVIRONMENT "MOCK_SCMI_INTERFACE=ioctl")
endif()
