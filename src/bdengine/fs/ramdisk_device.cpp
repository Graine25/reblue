/**
 * @file        bdengine/fs/ramdisk_device.cpp
 *
 * @brief       In-memory ramdisk Device implementation. See header for design.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License -- see LICENSE
 */

#include "bdengine/fs/ramdisk_device.h"
#include "bdengine/common/logging.h"

#include <rex/filesystem.h>
#include <rex/filesystem/vfs.h>
#include <rex/string.h>
#include <rex/string/buffer.h>

#include <algorithm>
#include <cstring>

using rex::X_STATUS;

namespace bd::fs {

namespace {

// Mount path is intentionally outside \Device\Harddisk0\ so the SDK's
// NullDevice (registered earlier, mount=\Device\Harddisk0) doesn't
// shadow lookups -- VirtualFileSystem picks the first device whose mount
// path is a prefix of the resolved path.
constexpr std::string_view kCacheMountPath = "\\Ramdisk";
constexpr std::string_view kCacheSymlink = "cache:";

bool g_mounted = false;
RamdiskDevice* g_device = nullptr;  // non-owning, valid while VFS owns the device

}  // namespace

RamdiskFile::RamdiskFile(uint32_t access, RamdiskEntry* entry, std::shared_ptr<RamdiskBlob> blob)
    : rex::filesystem::File(access, entry), blob_(std::move(blob)) {}

X_STATUS RamdiskFile::ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                                size_t* out_bytes_read) {
  if (!(file_access_ & rex::filesystem::FileAccess::kFileReadData)) {
    return X_STATUS_ACCESS_DENIED;
  }

  std::lock_guard lock(blob_->mutex);
  const auto& src = blob_->data;
  if (byte_offset >= src.size()) {
    if (out_bytes_read) *out_bytes_read = 0;
    return X_STATUS_END_OF_FILE;
  }

  size_t n = std::min(buffer.size(), src.size() - byte_offset);
  std::memcpy(buffer.data(), src.data() + byte_offset, n);
  if (out_bytes_read) *out_bytes_read = n;
  return X_STATUS_SUCCESS;
}

X_STATUS RamdiskFile::WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                                 size_t* out_bytes_written) {
  if (!(file_access_ & (rex::filesystem::FileAccess::kFileWriteData |
                        rex::filesystem::FileAccess::kFileAppendData))) {
    return X_STATUS_ACCESS_DENIED;
  }

  {
    std::lock_guard lock(blob_->mutex);
    size_t end = byte_offset + buffer.size();
    if (end > blob_->data.size()) {
      blob_->data.resize(end);
    }
    std::memcpy(blob_->data.data() + byte_offset, buffer.data(), buffer.size());
  }
  if (out_bytes_written) *out_bytes_written = buffer.size();

  // Keep the entry's reported size in sync so GetFileSize/FindFirstFile see
  // the new length immediately. Done outside the blob lock -- update() also
  // takes that lock, which would deadlock if held here.
  if (auto* e = entry()) {
    e->update();
  }
  return X_STATUS_SUCCESS;
}

X_STATUS RamdiskFile::SetLength(size_t length) {
  if (!(file_access_ & rex::filesystem::FileAccess::kFileWriteData)) {
    return X_STATUS_ACCESS_DENIED;
  }
  {
    std::lock_guard lock(blob_->mutex);
    blob_->data.resize(length);
  }
  if (auto* e = entry()) {
    e->update();
  }
  return X_STATUS_SUCCESS;
}

RamdiskEntry::RamdiskEntry(RamdiskDevice* device, RamdiskEntry* parent,
                           const std::string_view path)
    : rex::filesystem::Entry(device, parent, path) {
  attributes_ = rex::filesystem::kFileAttributeDirectory;
}

RamdiskEntry::RamdiskEntry(RamdiskDevice* device, RamdiskEntry* parent,
                           const std::string_view path, std::shared_ptr<RamdiskBlob> blob)
    : rex::filesystem::Entry(device, parent, path), blob_(std::move(blob)) {
  attributes_ = rex::filesystem::kFileAttributeNormal;
  if (blob_) {
    size_ = blob_->data.size();
    size_t bps = device->bytes_per_sector();
    allocation_size_ = (size_ + bps - 1) & ~(bps - 1);
  }
}

X_STATUS RamdiskEntry::Open(uint32_t desired_access, rex::filesystem::File** out_file) {
  if (attributes_ & rex::filesystem::kFileAttributeDirectory) {
    // Directories don't support read/write opens; the SDK only opens
    // directories for enumeration which doesn't go through ReadSync.
    *out_file = new RamdiskFile(desired_access, this, std::make_shared<RamdiskBlob>());
    return X_STATUS_SUCCESS;
  }
  if (!blob_) {
    return X_STATUS_NO_SUCH_FILE;
  }
  *out_file = new RamdiskFile(desired_access, this, blob_);
  return X_STATUS_SUCCESS;
}

bool RamdiskEntry::Truncate() {
  if (attributes_ & rex::filesystem::kFileAttributeDirectory) return false;
  if (!blob_) return false;
  std::lock_guard lock(blob_->mutex);
  blob_->data.clear();
  size_ = 0;
  allocation_size_ = 0;
  return true;
}

bool RamdiskEntry::SetAttributes(uint64_t attributes) {
  attributes_ = static_cast<uint32_t>(attributes);
  return true;
}
bool RamdiskEntry::SetCreateTimestamp(uint64_t timestamp) {
  create_timestamp_ = timestamp;
  return true;
}
bool RamdiskEntry::SetAccessTimestamp(uint64_t timestamp) {
  access_timestamp_ = timestamp;
  return true;
}
bool RamdiskEntry::SetWriteTimestamp(uint64_t timestamp) {
  write_timestamp_ = timestamp;
  return true;
}

void RamdiskEntry::update() {
  if (blob_) {
    std::lock_guard lock(blob_->mutex);
    size_ = blob_->data.size();
    size_t bps = device()->bytes_per_sector();
    allocation_size_ = (size_ + bps - 1) & ~(bps - 1);
  }
}

std::unique_ptr<rex::filesystem::Entry> RamdiskEntry::CreateEntryInternal(
    const std::string_view name, uint32_t attributes) {
  auto child_path = rex::string::utf8_join_guest_paths(path_, name);
  auto* dev = static_cast<RamdiskDevice*>(device_);
  if (attributes & rex::filesystem::kFileAttributeDirectory) {
    return std::unique_ptr<rex::filesystem::Entry>(new RamdiskEntry(dev, this, child_path));
  }
  return std::unique_ptr<rex::filesystem::Entry>(
      new RamdiskEntry(dev, this, child_path, std::make_shared<RamdiskBlob>()));
}

bool RamdiskEntry::DeleteEntryInternal(rex::filesystem::Entry* entry) {
  // Children are owned by the parent's children_ vector; the base Entry::Delete
  // will erase that ownership after we return true. The blob ref-count drops
  // automatically when its owning entry vanishes.
  (void)entry;
  return true;
}

X_STATUS RamdiskEntry::RenameEntryInternal(const std::vector<std::string_view>& path_parts) {
  // Names are tracked by the base Entry; nothing extra to update for ramdisk.
  (void)path_parts;
  return X_STATUS_SUCCESS;
}

RamdiskDevice::RamdiskDevice(const std::string_view mount_path)
    : rex::filesystem::Device(mount_path), name_("RamdiskDevice") {}

bool RamdiskDevice::Initialize() {
  auto* root = new RamdiskEntry(this, nullptr, "");
  root_entry_ = std::unique_ptr<rex::filesystem::Entry>(root);
  return true;
}

void RamdiskDevice::Dump(rex::string::StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

rex::filesystem::Entry* RamdiskDevice::ResolvePath(const std::string_view path) {
  auto global_lock = global_critical_region_.Acquire();
  return root_entry_->ResolvePath(path);
}

bool MountRamdisk(rex::filesystem::VirtualFileSystem* vfs) {
  if (g_mounted) return true;
  if (!vfs) {
    BD_ERROR("[ramdisk] no VFS to mount on");
    return false;
  }

  auto device = std::make_unique<RamdiskDevice>(kCacheMountPath);
  if (!device->Initialize()) {
    BD_ERROR("[ramdisk] device init failed");
    return false;
  }
  g_device = device.get();
  if (!vfs->RegisterDevice(std::move(device))) {
    BD_ERROR("[ramdisk] device register failed");
    g_device = nullptr;
    return false;
  }
  vfs->RegisterSymbolicLink(std::string(kCacheSymlink), std::string(kCacheMountPath));
  g_mounted = true;
  BD_INFO("[ramdisk] mounted cache:\\ as in-memory device");
  return true;
}

void ResetRamdisk() {
  if (!g_device) return;
  // Re-Initialize() rebuilds an empty root, dropping every blob's last
  // strong ref. Cheap because the root is just an empty directory entry.
  g_device->Initialize();
}

}  // namespace bd::fs
