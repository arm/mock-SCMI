// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#ifndef INCLUDE_SCMI_IOCTL_UAPI_HPP
#define INCLUDE_SCMI_IOCTL_UAPI_HPP

#include <cstdint>

#if defined(__linux__)
#  include <sys/ioctl.h>
#endif

// This mirrors the SCMI telemetry ioctl UAPI shape used by the ASTL ioctl collector.
// NOLINTBEGIN(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

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
  uint32_t exp;
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
#  define SCMI_IOCTL_MAGIC        0xF1
#  define SCMI_TLM_GET_INFO       _IOR(SCMI_IOCTL_MAGIC, 0x00, struct scmi_tlm_base_info)
#  define SCMI_TLM_GET_CFG        _IOWR(SCMI_IOCTL_MAGIC, 0x01, struct scmi_tlm_config)
#  define SCMI_TLM_SET_CFG        _IOWR(SCMI_IOCTL_MAGIC, 0x02, struct scmi_tlm_config)
#  define SCMI_TLM_GET_INTRVS     _IOWR(SCMI_IOCTL_MAGIC, 0x03, struct scmi_tlm_intervals)
#  define SCMI_TLM_GET_DE_CFG     _IOWR(SCMI_IOCTL_MAGIC, 0x04, struct scmi_tlm_de_config)
#  define SCMI_TLM_SET_DE_CFG     _IOWR(SCMI_IOCTL_MAGIC, 0x05, struct scmi_tlm_de_config)
#  define SCMI_TLM_GET_DE_INFO    _IOWR(SCMI_IOCTL_MAGIC, 0x06, struct scmi_tlm_de_info)
#  define SCMI_TLM_GET_DE_LIST    _IOWR(SCMI_IOCTL_MAGIC, 0x07, struct scmi_tlm_des_list)
#  define SCMI_TLM_GET_DE_VALUE   _IOWR(SCMI_IOCTL_MAGIC, 0x08, struct scmi_tlm_de_sample)
#  define SCMI_TLM_GET_ALL_CFG    _IOWR(SCMI_IOCTL_MAGIC, 0x09, struct scmi_tlm_de_config)
#  define SCMI_TLM_SET_ALL_CFG    _IOWR(SCMI_IOCTL_MAGIC, 0x0A, struct scmi_tlm_de_config)
#  define SCMI_TLM_GET_GRP_LIST   _IOWR(SCMI_IOCTL_MAGIC, 0x0B, struct scmi_tlm_grps_list)
#  define SCMI_TLM_GET_GRP_INFO   _IOWR(SCMI_IOCTL_MAGIC, 0x0C, struct scmi_tlm_grp_info)
#  define SCMI_TLM_GET_GRP_DESC   _IOWR(SCMI_IOCTL_MAGIC, 0x0D, struct scmi_tlm_grp_desc)
#  define SCMI_TLM_SINGLE_SAMPLE  _IOWR(SCMI_IOCTL_MAGIC, 0x0E, struct scmi_tlm_data_read)
#  define SCMI_TLM_BULK_READ      _IOWR(SCMI_IOCTL_MAGIC, 0x0F, struct scmi_tlm_data_read)
#  define SCMI_TLM_BATCH_READ     _IOWR(SCMI_IOCTL_MAGIC, 0x10, struct scmi_tlm_data_read)
#  define SCMI_TLM_GET_SHMTI_LIST _IOWR(SCMI_IOCTL_MAGIC, 0x11, struct scmi_tlm_shmtis_list)
#endif

// NOLINTEND(cppcoreguidelines-avoid-c-arrays,cppcoreguidelines-macro-usage,cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers)

#endif  // INCLUDE_SCMI_IOCTL_UAPI_HPP
