// SPDX-FileCopyrightText: 2026 Arm Limited and/or its affiliates <open-source-office@arm.com>
//
// SPDX-License-Identifier: Apache-2.0

#include "mock_scmi.hpp"

#include <algorithm>
#include <iostream>

#include "common.hpp"
#include "fsnode.hpp"
#include "protocol.hpp"
#include "protocol_scmi_ioctl.hpp"
#include "scmi_backend_selection.hpp"

namespace mock_scmi {

constexpr std::string_view kStartUnitTest = "eccf4f7c-d1b1-47f0-9d23-159f6d38b661";

static void AddDirectoryBuffer(fuse_req_t req, std::vector<char>& buf, const char* name, ino_t ino) {
  // (clang-format <20 incorrectly wants to add a space before the {})
  // clang-format off
  struct stat stbuf{};
  // clang-format on
  stbuf.st_ino = ino;

  off_t  old_size   = static_cast<off_t>(buf.size());
  size_t entry_size = fuse_add_direntry(req, nullptr, 0, name, nullptr, 0);
  buf.resize(old_size + entry_size);

  fuse_add_direntry(req, std::next(buf.data(), old_size), entry_size, name, &stbuf, static_cast<off_t>(buf.size()));
}

static void LowLevelInit(void* user_data, struct fuse_conn_info* conn) {
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(user_data);
  (void)conn;

  // Create the absolute root directory.
  fuse_user_data->root = FileSystemNode::CreateDirectory("/");

  if (fuse_user_data->scmi_backend_preference == ScmiBackendPreference::IOCTL) {
    InitIoctlProtocol(fuse_user_data->root.get(), fuse_user_data->tlm_json_path);
  } else {
    InitProtocol(fuse_user_data->root.get(), fuse_user_data->tlm_json_path);
  }

  std::cout << "Mock filesystem initialized! backend=" << BackendPreferenceName(fuse_user_data->scmi_backend_preference)
            << " - " << kStartUnitTest << std::endl;
}

static void LowLevelDestroy(void* user_data) { (void)user_data; }

static void LowLevelLookup(fuse_req_t req, fuse_ino_t parent, const char* name) {
  std::cout << "lookup: parent=" << parent << ", name=" << name << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* parent_node = FindNodeByIno(fuse_user_data->root.get(), parent);
  if (!parent_node) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (auto matching_child = std::ranges::find_if(parent_node->GetChildren(),
                                                 [&name](const auto& child) { return child->GetName() == name; });
      matching_child != parent_node->GetChildren().end()) {
    const auto& child = *matching_child;
    // (clang-format <20 incorrectly wants to add a space before the {})
    // clang-format off
    struct fuse_entry_param entry{};
    // clang-format on
    entry.ino           = child->GetIno();
    entry.attr_timeout  = kAttrTimeoutSec;
    entry.entry_timeout = kEntryTimeoutSec;
    entry.attr.st_ino   = child->GetIno();
    entry.attr.st_mode  = child->GetMode();

    if (child->GetType() == NodeType::DIRECTORY_NODE) {
      entry.attr.st_nlink = 2;  // Directory has at least 2 links (. and ..) define constants
    } else {
      std::string value = HandleProtocolRead(child.get());
      if (!value.empty()) {
        child->SetFileContent(value);
      }
      entry.attr.st_nlink = 1;
      entry.attr.st_size  = static_cast<off_t>(child->GetFileContent().size());
    }

    fuse_reply_entry(req, &entry);
    return;
  }
  fuse_reply_err(req, ENOENT);
}

static void LowLevelGetAttr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* file_info) {
  (void)file_info;
  std::cout << "getattr: ino=" << ino << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!node) {
    fuse_reply_err(req, ENOENT);
    return;
  }
  // (clang-format <20 incorrectly wants to add a space before the {})
  // clang-format off
  struct stat stbuf{};
  // clang-format on
  stbuf.st_ino  = node->GetIno();
  stbuf.st_mode = node->GetMode();

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    stbuf.st_nlink = kDirectoryLinkCount;  // Directory has at least 2 links (. and ..)
    stbuf.st_size  = kDefaultDirSize;
  } else {
    std::string value = HandleProtocolRead(node);
    if (!value.empty()) {
      node->SetFileContent(value);
    }
    stbuf.st_nlink = kFileLinkCount;
    stbuf.st_size  = static_cast<off_t>(node->GetFileContent().size());
  }

  stbuf.st_atime = time(nullptr);
  stbuf.st_mtime = time(nullptr);
  stbuf.st_ctime = time(nullptr);

  fuse_reply_attr(req, &stbuf, 1.0);
}

static FileSystemNode const* FindRequestedNode(fuse_req_t req, fuse_ino_t ino, NodeType expected_type) {
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode const* node = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!node) {
    fuse_reply_err(req, ENOENT);
    return nullptr;
  }

  if (node->GetType() != expected_type) {
    const auto error_code = (node->GetType() == NodeType::DIRECTORY_NODE) ? EISDIR : ENOTDIR;
    fuse_reply_err(req, error_code);
    return nullptr;
  }

  return node;
}

// cppcheck-suppress constParameterCallback //  (file_info is mutated by callback)
static void LowLevelOpen(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* file_info) {
  std::cout << "open: ino=" << ino << std::endl;
  FileSystemNode const* node = FindRequestedNode(req, ino, NodeType::FILE_NODE);
  if (!node) {
    return;
  }
  fuse_reply_open(req, file_info);
}

static int ReplyBufferLimited(fuse_req_t req, const char* buf, size_t bufsize, off_t off, size_t maxsize) {
  if (off >= 0 && static_cast<size_t>(off) < bufsize) {
    return fuse_reply_buf(req, std::next(buf, off), std::min(bufsize - static_cast<size_t>(off), maxsize));
  }

  return fuse_reply_buf(req, nullptr, 0);
}

static void LowLevelRead(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info* file_info) {
  (void)file_info;
  std::cout << "read: ino=" << ino << ", size=" << size << ", off=" << off << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!node) {
    fuse_reply_err(req, ENOENT);
    return;
  }

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    fuse_reply_err(req, EISDIR);
    return;
  }

  std::string value = HandleProtocolRead(node);
  if (!value.empty()) {
    node->SetFileContent(value);
  }
  std::cout << "value read: " << node->GetFileContent() << "\n";

  ReplyBufferLimited(req, node->GetFileContent().c_str(), node->GetFileContent().size(), off, size);
}

struct LowLevelWriteRequest {
  fuse_req_t             req;
  fuse_ino_t             ino;
  const char*            buffer;
  size_t                 size;
  off_t                  offset;
  struct fuse_file_info* file_info;
};

static void HandleLowLevelWrite(const LowLevelWriteRequest& request) {
  (void)request.file_info;
  std::cout << "write: ino=" << request.ino << ", size=" << request.size << ", off=" << request.offset << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(request.req));

  FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), request.ino);

  if (!node) {
    fuse_reply_err(request.req, ENOENT);
    return;
  }

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    fuse_reply_err(request.req, EISDIR);
    return;
  }

  if (HandleProtocolWrite(node, std::string(request.buffer, request.size)) != ErrorCode::SUCCESS) {
    fuse_reply_err(request.req, EIO);
    return;
  }

  if (request.offset + request.size > kMaxContentLen - 1) {
    fuse_reply_err(request.req, EFBIG);
    return;
  }

  // Resize the content if needed
  if (request.offset + request.size > node->GetFileContent().size()) {
    node->GetFileContent().resize(request.offset + request.size);
  }

  std::copy(request.buffer, std::next(request.buffer, static_cast<off_t>(request.size)),
            node->GetFileContent().begin() + request.offset);

  fuse_reply_write(request.req, request.size);
}

static constexpr auto LowLevelWrite = []<typename... Args>(Args... args) {
  static_assert(sizeof...(Args) == 6);
  HandleLowLevelWrite(LowLevelWriteRequest{args...});
};

// cppcheck-suppress constParameterCallback // (file_info is mutated by callback)
static void LowLevelOpenDir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info* file_info) {
  std::cout << "opendir: ino=" << ino << std::endl;
  FileSystemNode const* node = FindRequestedNode(req, ino, NodeType::DIRECTORY_NODE);
  if (!node) {
    return;
  }
  fuse_reply_open(req, file_info);
}

static void LowLevelReadDir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info* file_info) {
  (void)file_info;
  std::cout << "readdir: ino=" << ino << ", size=" << size << ", off=" << off << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(req));

  FileSystemNode const* dir = FindNodeByIno(fuse_user_data->root.get(), ino);
  if (!dir || dir->GetType() != NodeType::DIRECTORY_NODE) {
    fuse_reply_err(req, ENOTDIR);
    return;
  }

  std::vector<char> buf;

  // Add standard entries
  AddDirectoryBuffer(req, buf, ".", dir->GetIno());
  ino_t parent_ino = (dir->GetParent() ? dir->GetParent()->GetIno() : dir->GetIno());  // Check if root
  AddDirectoryBuffer(req, buf, "..", parent_ino);

  // Add children
  for (const auto& child : dir->GetChildren()) {
    AddDirectoryBuffer(req, buf, child->GetName().c_str(), child->GetIno());
  }

  // The 'off' parameter indicates how many bytes have already been read by the client.
  // This function will only return the remaining data starting at 'off', up to 'size' bytes.
  // This allows clients (like ls) to paginate the results if the entire directory listing is large.
  ReplyBufferLimited(req, buf.data(), buf.size(), off, size);
}

#if FUSE_USE_VERSION < 35
using FuseIoctlCommand = int;
#else
using FuseIoctlCommand = unsigned int;
#endif

struct LowLevelIoctlRequest {
  fuse_req_t       request;
  fuse_ino_t       inode;
  FuseIoctlCommand command;
  const void*      input_buffer;
  size_t           input_buffer_size;
  size_t           output_buffer_size;
};

static void HandleLowLevelIoctl(const LowLevelIoctlRequest& request) {
  std::cout << "ioctl: ino=" << request.inode << ", cmd=" << request.command
            << ", in_bufsz=" << request.input_buffer_size << ", out_bufsz=" << request.output_buffer_size << std::endl;
  FuseUserData* fuse_user_data = static_cast<FuseUserData*>(fuse_req_userdata(request.request));

  const FileSystemNode* node = FindNodeByIno(fuse_user_data->root.get(), request.inode);
  if (!node) {
    fuse_reply_err(request.request, ENOENT);
    return;
  }

  if (node->GetType() == NodeType::DIRECTORY_NODE) {
    fuse_reply_err(request.request, EISDIR);
    return;
  }

  auto reply = HandleProtocolIoctl(node, static_cast<unsigned int>(request.command), request.input_buffer,
                                   request.input_buffer_size);
  if (reply.error_code != 0) {
    fuse_reply_err(request.request, reply.error_code);
    return;
  }

  if (reply.output.empty()) {
    fuse_reply_ioctl(request.request, 0, nullptr, 0);
    return;
  }

  fuse_reply_ioctl(request.request, 0, reply.output.data(), std::min(reply.output.size(), request.output_buffer_size));
}

const struct fuse_lowlevel_ops kFuseLowLevelOps = {
    .init            = LowLevelInit,
    .destroy         = LowLevelDestroy,
    .lookup          = LowLevelLookup,
    .forget          = NULL,
    .getattr         = LowLevelGetAttr,
    .setattr         = NULL,
    .readlink        = NULL,
    .mknod           = NULL,
    .mkdir           = NULL,
    .unlink          = NULL,
    .rmdir           = NULL,
    .symlink         = NULL,
    .rename          = NULL,
    .link            = NULL,
    .open            = LowLevelOpen,
    .read            = LowLevelRead,
    .write           = LowLevelWrite,
    .flush           = NULL,
    .release         = NULL,
    .fsync           = NULL,
    .opendir         = LowLevelOpenDir,
    .readdir         = LowLevelReadDir,
    .releasedir      = NULL,
    .fsyncdir        = NULL,
    .statfs          = NULL,
    .setxattr        = NULL,
    .getxattr        = NULL,
    .listxattr       = NULL,
    .removexattr     = NULL,
    .access          = NULL,
    .create          = NULL,
    .getlk           = NULL,
    .setlk           = NULL,
    .bmap            = NULL,
    .ioctl           = [](fuse_req_t req, fuse_ino_t ino, FuseIoctlCommand                      cmd, void* /*arg*/,
                struct fuse_file_info* /*file_info*/, unsigned /*flags*/, const void* in_buf, size_t in_bufsz,
                size_t out_bufsz) { HandleLowLevelIoctl({req, ino, cmd, in_buf, in_bufsz, out_bufsz}); },
    .poll            = NULL,
    .write_buf       = NULL,
    .retrieve_reply  = NULL,
    .forget_multi    = NULL,
    .flock           = NULL,
    .fallocate       = NULL,
    .readdirplus     = NULL,
    .copy_file_range = NULL,
    .lseek           = NULL,
};

}  // namespace mock_scmi
