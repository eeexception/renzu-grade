// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Engine.h"
#include <filesystem>
namespace renzu {
constexpr int referenceSlots = 16;
constexpr std::size_t maxReferenceSamples = 4096;
struct ReferenceFrame {
    std::string id, name;
    int slot = 0, space = 0;
    double time = 0;
    // Decoded linear RGB at the reference instance's OUTPUT, before downstream nodes.
    std::vector<V3> pixels;
};
enum class MatchMode : int { WBAndExposure = 0, Exposure = 1, WhiteBalance = 2 };
struct MatchFields {
    bool whiteBalance = true, exposure = true, contrast = false, color = false;
};
struct MatchResult {
    Settings settings;
    bool exposureMatched = false, whiteBalanceMatched = false;
    bool contrastMatched = false, colorMatched = false;
    bool limited = false;
    double confidence = 0;
    std::string status;
    bool applied() const {
        return exposureMatched || whiteBalanceMatched || contrastMatched || colorMatched;
    }
};
V3 convertLinear(V3 rgb, int fromSpace, int toSpace);
std::vector<V3> gradeSamples(const std::vector<V3> &rawLinear, const Settings &settings);
ReferenceFrame makeReference(const std::vector<V3> &rawLinear, const Settings &settings,
                             const std::string &name, double time);
MatchResult matchReference(const std::vector<V3> &rawLinear, const Settings &baseline,
                           const ReferenceFrame &reference, MatchMode mode);
MatchResult matchReference(const std::vector<V3> &rawLinear, const Settings &baseline,
                           const ReferenceFrame &reference, MatchFields fields);
// Bounded, checksummed, portable text, also suitable for a persistent OFX string.
std::string serializeReference(const ReferenceFrame &reference);
ReferenceFrame deserializeReference(const std::string &text);
struct ReferenceSummary {
    int slot = 0;
    std::string id, name;
};
class ReferenceStore {
  public:
    explicit ReferenceStore(std::filesystem::path directory) : directory_(std::move(directory)) {}
    static std::filesystem::path defaultDirectory();
    const std::filesystem::path &directory() const {
        return directory_;
    }
    std::vector<ReferenceSummary> list() const;
    // Slot 0 allocates an empty slot; other slots explicitly replace that slot.
    ReferenceFrame save(ReferenceFrame frame, int slot = 0) const;
    ReferenceFrame load(int slot) const; // 0 = most recently saved, via persistent latest pointer
  private:
    std::filesystem::path directory_;
};
} // namespace renzu
