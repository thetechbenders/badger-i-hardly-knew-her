#include <vector>

#include "assetpack.hpp"
#include "check.hpp"

namespace badge {
extern const uint8_t kBuiltinAssetPack[];
extern const size_t kBuiltinAssetPack_size;
}  // namespace badge

using namespace badge;

TEST(assetpack_builtin_valid) {
  AssetPackInfo i = asset_pack_validate(kBuiltinAssetPack, kBuiltinAssetPack_size);
  CHECK(i.status == AssetStatus::Ok);
  MonoBitmap p;
  CHECK(asset_pack_bitmap(kBuiltinAssetPack, kAssetIdPortrait, &p));
  CHECK(p.width <= 296 && p.height <= 128);
  CHECK(!asset_pack_bitmap(kBuiltinAssetPack, 999, &p));
}

TEST(assetpack_detects_corruption) {
  std::vector<uint8_t> good(kBuiltinAssetPack, kBuiltinAssetPack + kBuiltinAssetPack_size);
  int undetected = 0;
  for (size_t i = 0; i < good.size(); i += 5) {
    std::vector<uint8_t> b = good;
    b[i] ^= 0x10;
    if (asset_pack_validate(b.data(), b.size()).status == AssetStatus::Ok) ++undetected;
  }
  CHECK_EQ(undetected, 0);
  CHECK(asset_pack_validate(good.data(), good.size() - 1).status == AssetStatus::BadSize);  // truncated
  std::vector<uint8_t> erased(4096, 0xFF);
  CHECK(asset_pack_validate(erased.data(), erased.size()).status == AssetStatus::Missing);
  CHECK(asset_pack_validate(nullptr, 0).status == AssetStatus::Missing);
}
