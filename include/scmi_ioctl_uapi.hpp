// SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef MOCK_SCMI_IOCTL_UAPI_HPP_
#define MOCK_SCMI_IOCTL_UAPI_HPP_

#include <cstdint>

#if defined(__linux__)
#  include <sys/ioctl.h>
#endif

// This mirrors the SCMI telemetry ioctl UAPI shape used by ASTL tools.
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-macro-usage,readability-magic-numbers)

/** @brief First SCMI telemetry userspace ABI version. */
#define SCMI_TLM_ABI_VERSION_V1 1

/** @brief Latest ABI version understood by this UAPI mirror. */
#define SCMI_TLM_CURRENT_ABI_VERSION SCMI_TLM_ABI_VERSION_V1

/** @brief Number of bytes used by the data-event implementation UUID. */
#define SCMI_TLM_DE_IMPL_UUID_MAX 16

struct scmi_tlm_abi_info {
  uint32_t size;
  uint32_t abi_version;
  uint32_t abi_features;

#define SCMI_TLM_ABI_FEAT_RESET (1U << 0)

  uint8_t  de_impl_version[SCMI_TLM_DE_IMPL_UUID_MAX];
  uint32_t num_des;
  uint32_t num_groups;
  uint32_t num_intervals;
  uint32_t num_shmtis;
  uint32_t features;

#define SCMI_TLM_BASE_SUPPORT_RESET               (1U << 0)
#define SCMI_TLM_BASE_SUPPORT_SINGLE_SAMPLE       (1U << 1)
#define SCMI_TLM_BASE_SUPPORT_GROUP_CONFIG        (1U << 2)
#define SCMI_TLM_BASE_SUPPORT_UPDATE_NOTIFICATION (1U << 3)

  uint64_t reserved;
};

// Legacy pre-V6 UAPI retained so old tools can use the same mock.
#define SCMI_TLM_DE_IMPL_MAX_DWORDS 4

struct scmi_tlm_base_info {
  uint32_t version;
  uint32_t de_impl_version[SCMI_TLM_DE_IMPL_MAX_DWORDS];
  uint32_t num_des;
  uint32_t num_groups;
  uint32_t num_intervals;
  uint32_t num_shmtis;
  uint32_t flags;
};

struct scmi_tlm_update_interval {
  uint32_t secs;
  int32_t  exp;
};

struct scmi_tlm_config {
  uint8_t                  enable;
  uint8_t                  t_enable;
  uint8_t                  flags;
  uint8_t                  pad;
  uint32_t                 grp_id;
  scmi_tlm_update_interval active;
};

struct scmi_tlm_intervals {
  uint32_t grp_id;
  uint8_t  flags;
  uint8_t  pad[3];
  uint32_t num_intervals;
  uint32_t pad2;
  uint64_t intervals;
};

struct scmi_tlm_de_config {
  uint32_t id;
  uint32_t enable;
  uint32_t t_enable;
  uint32_t pad;
};

struct scmi_tlm_de_info {
  uint32_t id;
  uint32_t grp_id;
  uint32_t data_sz;
  uint32_t type;
  uint32_t unit;
  int32_t  unit_exp;
  uint32_t ts_rate;
  uint32_t instance_id;
  uint32_t compo_instance_id;
  uint32_t compo_type;
  uint8_t  persistent;
  uint8_t  flags;
  uint8_t  pad[2];
  uint32_t pad2;
  uint8_t  name[16];
};

struct scmi_tlm_des_list {
  uint32_t num_des;
  uint32_t pad;
  uint64_t des;
};

struct scmi_tlm_de_sample {
  uint32_t id;
  uint32_t pad;
  uint64_t tstamp;
  uint64_t val;
};

struct scmi_tlm_data_read {
  uint32_t grp_id;
  uint8_t  flags;
  uint8_t  pad[3];
  uint32_t pad2;
  uint32_t num_samples;
  uint64_t samples;
};

struct scmi_tlm_grp_info {
  uint32_t grp_id;
  uint32_t num_des;
  uint32_t num_intervals;
  uint32_t pad;
};

struct scmi_tlm_grps_list {
  uint32_t num_grps;
  uint32_t pad;
  uint64_t grps;
};

struct scmi_tlm_grp_desc {
  uint32_t grp_id;
  uint32_t num_des;
  uint64_t composing_des;
};

struct scmi_tlm_shmti_info {
  uint32_t sid;
  uint32_t fd;
  uint32_t len;
  uint32_t offset;
};

struct scmi_tlm_shmtis_list {
  uint32_t num_shmtis;
  uint32_t pad;
  uint64_t shmtis;
};

#if defined(__linux__)
#  define SCMI_TLM_IOCTL_MAGIC 0xF1

#  define SCMI_TLM_GET_ABI_INFO   _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x00, struct scmi_tlm_abi_info)
#  define SCMI_TLM_GET_CFG        _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x01, struct scmi_tlm_config)
#  define SCMI_TLM_SET_CFG        _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x02, struct scmi_tlm_config)
#  define SCMI_TLM_GET_INTRVS     _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x03, struct scmi_tlm_intervals)
#  define SCMI_TLM_GET_DE_CFG     _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x04, struct scmi_tlm_de_config)
#  define SCMI_TLM_SET_DE_CFG     _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x05, struct scmi_tlm_de_config)
#  define SCMI_TLM_GET_DE_INFO    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x06, struct scmi_tlm_de_info)
#  define SCMI_TLM_GET_DE_LIST    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x07, struct scmi_tlm_des_list)
#  define SCMI_TLM_DE_READ        _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x08, struct scmi_tlm_de_sample)
#  define SCMI_TLM_GET_ALL_CFG    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x09, struct scmi_tlm_de_config)
#  define SCMI_TLM_SET_ALL_CFG    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0A, struct scmi_tlm_de_config)
#  define SCMI_TLM_GET_GRP_LIST   _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0B, struct scmi_tlm_grps_list)
#  define SCMI_TLM_GET_GRP_INFO   _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0C, struct scmi_tlm_grp_info)
#  define SCMI_TLM_GET_GRP_DESC   _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0D, struct scmi_tlm_grp_desc)
#  define SCMI_TLM_SINGLE_READ    _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0E, struct scmi_tlm_data_read)
#  define SCMI_TLM_BULK_READ      _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x0F, struct scmi_tlm_data_read)
#  define SCMI_TLM_BATCH_READ     _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x10, struct scmi_tlm_data_read)
#  define SCMI_TLM_GET_SHMTI_LIST _IOWR(SCMI_TLM_IOCTL_MAGIC, 0x11, struct scmi_tlm_shmtis_list)
#  define SCMI_TLM_RESET          _IO(SCMI_TLM_IOCTL_MAGIC, 0x12)

// Source-compatible names for clients built against the pre-V6 UAPI. GET_INFO
// remains a distinct encoded request because its payload and direction differ.
#  define SCMI_IOCTL_MAGIC       SCMI_TLM_IOCTL_MAGIC
#  define SCMI_TLM_GET_INFO      _IOR(SCMI_TLM_IOCTL_MAGIC, 0x00, struct scmi_tlm_base_info)
#  define SCMI_TLM_GET_DE_VALUE  SCMI_TLM_DE_READ
#  define SCMI_TLM_SINGLE_SAMPLE SCMI_TLM_SINGLE_READ
#endif

static_assert(sizeof(scmi_tlm_abi_info) == 56, "Unexpected SCMI telemetry ABI info layout");
static_assert(sizeof(scmi_tlm_update_interval) == 8, "Unexpected SCMI telemetry interval layout");

// NOLINTEND(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-avoid-magic-numbers,cppcoreguidelines-macro-usage,readability-magic-numbers)

#endif  // MOCK_SCMI_IOCTL_UAPI_HPP_
