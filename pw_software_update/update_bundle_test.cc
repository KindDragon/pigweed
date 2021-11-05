// Copyright 2021 The Pigweed Authors
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not
// use this file except in compliance with the License. You may obtain a copy of
// the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
// WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
// License for the specific language governing permissions and limitations under
// the License.

#include <array>

#include "gtest/gtest.h"
#include "pw_kvs/fake_flash_memory.h"
#include "pw_kvs/test_key_value_store.h"
#include "pw_software_update/bundled_update_backend.h"
#include "pw_software_update/update_bundle_accessor.h"
#include "pw_stream/memory_stream.h"
#include "test_bundles.h"

#define ASSERT_OK(status) ASSERT_EQ(OkStatus(), status)
#define ASSERT_NOT_OK(status) ASSERT_NE(OkStatus(), status)

namespace pw::software_update {
namespace {

constexpr size_t kBufferSize = 256;
static constexpr size_t kFlashAlignment = 16;
constexpr size_t kSectorSize = 2048;
constexpr size_t kSectorCount = 2;
constexpr size_t kMetadataBufferSize =
    blob_store::BlobStore::BlobWriter::RequiredMetadataBufferSize(0);

class TestBundledUpdateBackend final : public BundledUpdateBackend {
 public:
  TestBundledUpdateBackend() : trusted_root_reader_({}) {}

  Status ApplyReboot() override { return Status::Unimplemented(); }
  Status PostRebootFinalize() override { return OkStatus(); }

  Status ApplyTargetFile(std::string_view, stream::Reader&, size_t) override {
    return OkStatus();
  }

  Result<uint32_t> EnableBundleTransferHandler(std::string_view) override {
    return 0;
  }

  void DisableBundleTransferHandler() override {}

  void SetTrustedRoot(ConstByteSpan trusted_root) {
    trusted_root_reader_ = stream::MemoryReader(trusted_root);
  }

  virtual Result<stream::SeekableReader*> GetRootMetadataReader() override {
    return &trusted_root_reader_;
  };

  virtual Status SafelyPersistRootMetadata(
      [[maybe_unused]] stream::Reader& root_metadata) override {
    new_root_persisted_ = true;
    return OkStatus();
  };

  bool IsNewRootPersisted() const { return new_root_persisted_; }

 private:
  stream::MemoryReader trusted_root_reader_;
  bool new_root_persisted_ = false;
};

class UpdateBundleTest : public testing::Test {
 public:
  UpdateBundleTest()
      : blob_flash_(kFlashAlignment),
        blob_partition_(&blob_flash_),
        bundle_blob_("TestBundle",
                     blob_partition_,
                     nullptr,
                     kvs::TestKvs(),
                     kBufferSize) {}

  blob_store::BlobStoreBuffer<kBufferSize>& bundle_blob() {
    return bundle_blob_;
  }

  TestBundledUpdateBackend& backend() { return backend_; }

  void StageTestBundle(ConstByteSpan bundle_data) {
    ASSERT_OK(bundle_blob_.Init());
    blob_store::BlobStore::BlobWriter blob_writer(bundle_blob(),
                                                  metadata_buffer_);
    ASSERT_OK(blob_writer.Open());
    ASSERT_OK(blob_writer.Write(bundle_data));
    ASSERT_OK(blob_writer.Close());
  }

 private:
  kvs::FakeFlashMemoryBuffer<kSectorSize, kSectorCount> blob_flash_;
  kvs::FlashPartition blob_partition_;
  blob_store::BlobStoreBuffer<kBufferSize> bundle_blob_;
  std::array<std::byte, kMetadataBufferSize> metadata_buffer_;
  TestBundledUpdateBackend backend_;
};

}  // namespace

TEST_F(UpdateBundleTest, GetTargetPayload) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestDevBundle);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend());

  ManifestAccessor current_manifest;
  ASSERT_OK(update_bundle.OpenAndVerify(current_manifest));

  {
    stream::IntervalReader res = update_bundle.GetTargetPayload("file1");
    ASSERT_OK(res.status());

    const char kExpectedContent[] = "file 1 content";
    char read_buffer[sizeof(kExpectedContent) + 1] = {0};
    ASSERT_TRUE(res.Read(read_buffer, sizeof(kExpectedContent)).ok());
    ASSERT_STREQ(read_buffer, kExpectedContent);
  }

  {
    stream::IntervalReader res = update_bundle.GetTargetPayload("file2");
    ASSERT_OK(res.status());

    const char kExpectedContent[] = "file 2 content";
    char read_buffer[sizeof(kExpectedContent) + 1] = {0};
    ASSERT_TRUE(res.Read(read_buffer, sizeof(kExpectedContent)).ok());
    ASSERT_STREQ(read_buffer, kExpectedContent);
  }

  {
    stream::IntervalReader res = update_bundle.GetTargetPayload("non-exist");
    ASSERT_EQ(res.status(), Status::NotFound());
  }
}

TEST_F(UpdateBundleTest, IsTargetPayloadIncluded) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestDevBundle);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend());

  ManifestAccessor current_manifest;
  ASSERT_OK(update_bundle.OpenAndVerify(current_manifest));

  Result<bool> res = update_bundle.IsTargetPayloadIncluded("file1");
  ASSERT_OK(res.status());
  ASSERT_TRUE(res.value());

  res = update_bundle.IsTargetPayloadIncluded("file2");
  ASSERT_OK(res.status());
  ASSERT_TRUE(res.value());

  res = update_bundle.IsTargetPayloadIncluded("non-exist");
  ASSERT_OK(res.status());
  ASSERT_FALSE(res.value());
}

TEST_F(UpdateBundleTest, PersistManifest) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestDevBundle);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend());

  ManifestAccessor current_manifest;
  ASSERT_OK(update_bundle.OpenAndVerify(current_manifest));

  std::byte manifest_buffer[sizeof(kTestBundleManifest)];
  stream::MemoryWriter manifest_writer(manifest_buffer);
  ASSERT_OK(update_bundle.PersistManifest(manifest_writer));

  ASSERT_EQ(
      memcmp(manifest_buffer, kTestBundleManifest, sizeof(kTestBundleManifest)),
      0);
}

TEST_F(UpdateBundleTest, PersistManifestFailIfNotVerified) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestBadDevSignatureBundle);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend());

  ManifestAccessor current_manifest;
  ASSERT_NOT_OK(update_bundle.OpenAndVerify(current_manifest));

  std::byte manifest_buffer[sizeof(kTestBundleManifest)];
  stream::MemoryWriter manifest_writer(manifest_buffer);
  ASSERT_NOT_OK(update_bundle.PersistManifest(manifest_writer));
}

TEST_F(UpdateBundleTest, BundleVerificationDisabled) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestBadDevSignatureBundle);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend(), true);

  // Since bundle verification is disabled. The bad bundle should not report
  // error.
  ManifestAccessor current_manifest;
  ASSERT_OK(update_bundle.OpenAndVerify(current_manifest));

  // Manifest persisting should be allowed as well.
  std::byte manifest_buffer[sizeof(kTestBundleManifest)];
  stream::MemoryWriter manifest_writer(manifest_buffer);
  ASSERT_OK(update_bundle.PersistManifest(manifest_writer));

  ASSERT_EQ(
      memcmp(manifest_buffer, kTestBundleManifest, sizeof(kTestBundleManifest)),
      0);
}

TEST_F(UpdateBundleTest, SignatureVerificationSucceeds) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestProdBundle);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend());

  ManifestAccessor current_manifest;
  ASSERT_FALSE(backend().IsNewRootPersisted());
  ASSERT_OK(update_bundle.OpenAndVerify(current_manifest));
  ASSERT_TRUE(backend().IsNewRootPersisted());
}

TEST_F(UpdateBundleTest, OpenAndVerifyFailsOnBadDevSignature) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestBadDevSignatureBundle);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend());

  ManifestAccessor current_manifest;
  ASSERT_FALSE(backend().IsNewRootPersisted());
  ASSERT_NOT_OK(update_bundle.OpenAndVerify(current_manifest));
  ASSERT_FALSE(backend().IsNewRootPersisted());
}

TEST_F(UpdateBundleTest, OpenAndVerifyFailsOnBadProdSignature) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestBadProdSignature);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend());

  ManifestAccessor current_manifest;
  ASSERT_FALSE(backend().IsNewRootPersisted());
  ASSERT_NOT_OK(update_bundle.OpenAndVerify(current_manifest));
  ASSERT_FALSE(backend().IsNewRootPersisted());
}

TEST_F(UpdateBundleTest, OpenAndVerifyFailsOnBadTargetsSignature) {
  backend().SetTrustedRoot(kDevSignedRoot);
  StageTestBundle(kTestBadTargetsSignature);
  UpdateBundleAccessor update_bundle(bundle_blob(), backend());

  ManifestAccessor current_manifest;
  ASSERT_NOT_OK(update_bundle.OpenAndVerify(current_manifest));
}

}  // namespace pw::software_update
