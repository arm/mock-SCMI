# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13) # target link directories support
project(MockScmi LANGUAGES CXX)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE3 REQUIRED fuse3)
find_package(nlohmann_json CONFIG REQUIRED)

file(GLOB_RECURSE MOCK_SCMI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(MockScmi ${MOCK_SCMI_SOURCES})
add_executable(MockScmiIoctlSmokeTest ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/ioctl_smoke_test.cpp)

include(CTest)
enable_testing()

add_test(
  NAME toolsMockScmiFind
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/kernel_test_fixture.sh $<TARGET_FILE:MockScmi>
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/find/expected.txt ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/find/command.sh)
set_tests_properties(toolsMockScmiFind PROPERTIES LABELS "integration;mockscmi;valgrind_isolated" SKIP_RETURN_CODE 77)

add_test(
  NAME toolsMockScmiCdPermissions
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/kernel_test_fixture.sh $<TARGET_FILE:MockScmi>
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/cd_permissions/expected.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/cd_permissions/command.sh)
set_tests_properties(toolsMockScmiCdPermissions PROPERTIES LABELS "integration;mockscmi;valgrind_isolated" SKIP_RETURN_CODE 77)

add_test(
  NAME toolsMockScmiAllDesEnableToIndividualEnableConsistency
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/kernel_test_fixture.sh $<TARGET_FILE:MockScmi>
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/all_des_enable_to_individual_enable_consistency/expected.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/all_des_enable_to_individual_enable_consistency/command.sh)
set_tests_properties(toolsMockScmiAllDesEnableToIndividualEnableConsistency PROPERTIES LABELS "integration;mockscmi;valgrind_isolated"
                                                                                       SKIP_RETURN_CODE 77)

add_test(
  NAME toolsMockScmiIoctlBackend
  COMMAND
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/kernel_test_fixture.sh $<TARGET_FILE:MockScmi>
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/ioctl_backend/expected.txt bash
    ${CMAKE_CURRENT_SOURCE_DIR}/kernel_tests/ioctl_backend/command.sh $<TARGET_FILE:MockScmiIoctlSmokeTest>)
set_tests_properties(toolsMockScmiIoctlBackend PROPERTIES LABELS "integration;mockscmi;valgrind_isolated" SKIP_RETURN_CODE 77 ENVIRONMENT
                                                          "ASTL_SCMI_INTERFACE=ioctl")

target_compile_definitions(MockScmi PRIVATE FUSE_USE_VERSION=316 # make sure to update scripts/lint.sh if this changes
)

target_include_directories(MockScmi PRIVATE ${FUSE3_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/include
                                            ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_include_directories(MockScmiIoctlSmokeTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
                                                          ${CMAKE_CURRENT_SOURCE_DIR}/../include)

target_link_directories(MockScmi PRIVATE ${FUSE3_LIBRARY_DIRS})

target_link_libraries(MockScmi PRIVATE ${FUSE3_LIBRARIES} nlohmann_json::nlohmann_json)
