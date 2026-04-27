/**
 * @file        bdengine/fs/ramdisk_device.h
 *
 * @brief       In-memory ramdisk Device for the cache:\ partition.
 *
 *              Implements rex::filesystem::Device/Entry/File so the SDK's
 *              CRT layer (CreateFileA/ReadFile/WriteFile/...) can serve
 *              cache:\ paths without ever touching disk. Files written here
 *              live entirely in RAM and are shared across handles to the
 *              same path.
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 * @license     BSD 3-Clause License -- see LICENSE
 */
#pragma once

#include <rex/filesystem/device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/system/xtypes.h>

#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rex::filesystem {
class VirtualFileSystem;
}

namespace bd::fs {

// Mount the in-memory ramdisk as cache:\ on the given VFS. Safe to call
// multiple times; subsequent calls are no-ops.
bool MountRamdisk(rex::filesystem::VirtualFileSystem* vfs);

// Drop everything in the ramdisk (test/dev only).
void ResetRamdisk();

// Shared blob backing a single ramdisk file.
struct RamdiskBlob {
  std::mutex mutex;
  std::vector<uint8_t> data;
};

class RamdiskDevice;
class RamdiskEntry;

class RamdiskFile : public rex::filesystem::File {
 public:
  RamdiskFile(uint32_t access, RamdiskEntry* entry, std::shared_ptr<RamdiskBlob> blob);
  ~RamdiskFile() override = default;

  void Destroy() override { delete this; }

  rex::X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                         size_t* out_bytes_read) override;
  rex::X_STATUS WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                          size_t* out_bytes_written) override;
  rex::X_STATUS SetLength(size_t length) override;

 private:
  std::shared_ptr<RamdiskBlob> blob_;
};

class RamdiskEntry : public rex::filesystem::Entry {
 public:
  // Directory entry.
  RamdiskEntry(RamdiskDevice* device, RamdiskEntry* parent, const std::string_view path);
  // File entry (takes ownership of an existing blob, or creates a fresh one).
  RamdiskEntry(RamdiskDevice* device, RamdiskEntry* parent, const std::string_view path,
               std::shared_ptr<RamdiskBlob> blob);

  ~RamdiskEntry() override = default;

  rex::X_STATUS Open(uint32_t desired_access, rex::filesystem::File** out_file) override;
  bool Truncate() override;

  bool SetAttributes(uint64_t attributes) override;
  bool SetCreateTimestamp(uint64_t timestamp) override;
  bool SetAccessTimestamp(uint64_t timestamp) override;
  bool SetWriteTimestamp(uint64_t timestamp) override;

  void update() override;

  std::shared_ptr<RamdiskBlob> blob() const { return blob_; }

 private:
  std::unique_ptr<rex::filesystem::Entry> CreateEntryInternal(const std::string_view name,
                                                               uint32_t attributes) override;
  bool DeleteEntryInternal(rex::filesystem::Entry* entry) override;
  void RenameEntryInternal(const std::vector<std::string_view>& path_parts) override;

  // Null for directory entries.
  std::shared_ptr<RamdiskBlob> blob_;
};

class RamdiskDevice : public rex::filesystem::Device {
 public:
  explicit RamdiskDevice(const std::string_view mount_path);
  ~RamdiskDevice() override = default;

  bool Initialize() override;
  void Dump(rex::string::StringBuffer* string_buffer) override;
  rex::filesystem::Entry* ResolvePath(const std::string_view path) override;

  bool is_read_only() const override { return false; }

  const std::string& name() const override { return name_; }
  uint32_t attributes() const override { return 0; }
  uint32_t component_name_max_length() const override { return 255; }

  // Pretend to have a reasonable amount of free space so games that check
  // free-space don't panic. Total/avail values are in allocation units.
  uint32_t total_allocation_units() const override { return 0x40000; }
  uint32_t available_allocation_units() const override { return 0x40000; }
  uint32_t sectors_per_allocation_unit() const override { return 0x80; }
  uint32_t bytes_per_sector() const override { return 0x200; }

 private:
  std::string name_;
  std::unique_ptr<rex::filesystem::Entry> root_entry_;
};

}  // namespace bd::fs
