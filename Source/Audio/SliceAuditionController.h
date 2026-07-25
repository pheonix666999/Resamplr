#pragma once

#include "Audio/PreviewPlayer.h"
#include "Chopping/SliceModel.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace padflow {
class SliceAuditionController final {
  public:
    explicit SliceAuditionController(PreviewPlayer& player) noexcept;

    [[nodiscard]] bool startSelected(std::shared_ptr<const SampleAsset> asset,
                                     const SliceRegion& slice, bool reverse = false);
    [[nodiscard]] bool startSequential(std::shared_ptr<const SampleAsset> asset,
                                       const std::vector<SliceRegion>& slices,
                                       bool reverse = false);
    [[nodiscard]] bool startLazy(std::shared_ptr<const SampleAsset> asset, std::int64_t trimStart,
                                 std::int64_t trimEnd);
    [[nodiscard]] bool stop() noexcept;
    void service();
    void clearWhenQuiescent() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] bool sequential() const noexcept;
    [[nodiscard]] std::uint64_t sourceFramePosition() const noexcept;

  private:
    [[nodiscard]] bool startSlice(const SliceRegion& slice);
    void retireActive(std::uint64_t epoch);

    PreviewPlayer& player_;
    DeferredSampleAssetReclaimer reclaimer_;
    std::shared_ptr<const SampleAsset> activeAsset_;
    std::vector<SliceRegion> sequence_;
    std::size_t sequenceIndex_{0U};
    std::uint64_t epoch_{0U};
    bool reverse_{false};
};
} // namespace padflow
