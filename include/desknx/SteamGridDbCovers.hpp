#pragma once

// SteamGridDB cover-art domain: state machine for one title's artwork,
// fallback chain (SteamGridDB -> GameTDB -> eShop banner), cache-key helpers,
// and presentation rules.
//
// What is NOT here:
//   - HTTP calls (belong in a platform service with curl/nx_http)
//   - File I/O (belongs in ArtworkCache)
//   - JSON parsing (belongs in the network layer)
//   - Rendering (belongs in BackdropWidget / TileWidget)
//
// A `CoverStore` manages a set of `TitleCoverState` objects.  The network
// layer calls `apply*()` from a background thread; the render thread only
// calls const accessors.  The two sides are separated by a generation counter
// so the render side never sees a half-written state.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace desknx::artwork {

using TitleId = std::uint64_t;

// ---- Fallback chain -------------------------------------------------------
//
// Exactly mirrors HomeNX:  SteamGridDB (if key) → GameTDB → eShop banner.
// ArtworkSource documents which level actually delivered the image.

enum class ArtworkSource : unsigned char {
    None,         // no artwork resolved yet
    EshopBanner,  // last-resort: use the icon or banner baked into the title
    GameTdb,      // GameTDB high-res cover
    SteamGridDb,  // SteamGridDB hero/logo chosen by the user or auto-matched
};

// Which artwork kind the caller wants from SteamGridDB.
// Matches HomeNX SteamGridDbManager::ArtworkKind.
enum class ArtworkKind : unsigned char {
    None,
    Hero,  // 1280x720 landscape background
    Logo,  // 640x180 transparent logo overlay
    Icon,  // square icon
};

// ---- Resolution status for one title --------------------------------------

enum class CoverStatus : unsigned char {
    Idle,         // not yet requested
    Pending,      // fetch or cache-load queued
    Loading,      // actively in-flight
    Ready,        // artwork available, source is set
    Error,        // all sources failed; show fallback icon
    NoKey,        // SteamGridDB requested but no API key — skip to GameTDB
    NoCover,      // SteamGridDB had no artwork for this title
    Ambiguous,    // SteamGridDB returned multiple close matches → picker needed
};

// Minimum similarity score for an auto-match (from HomeNX kMinimumMatchScore).
constexpr float kAutoMatchThreshold   = 0.78f;
// A second candidate this close to the first makes the result ambiguous.
constexpr float kAmbiguityMargin      = 0.04f;
// A "clear winner" suppresses the picker (1 candidate, or margin ≥ this).
constexpr float kClearWinnerMargin    = 0.08f;
// Minimum match score above which the auto-score is accepted as clear.
constexpr float kClearWinnerMinScore  = 0.92f;
// Artwork cache matcher version. Increment to force a re-fetch when the
// matching algorithm changes (matches HomeNX kMatcherVersion 4).
constexpr int   kMatcherVersion       = 4;

// ---- Key helpers ----------------------------------------------------------
//
// Cache lives at `sdmc:/switch/desknx/artwork/{TITLEID_HEX}/` — mirrors the
// HomeNX SteamGridDbManager::kCacheRoot path convention.

inline std::string artworkDir(TitleId titleId) {
    char id[17]{};
    std::snprintf(id, sizeof(id), "%016llX",
                  static_cast<unsigned long long>(titleId));
    return std::string("sdmc:/switch/desknx/artwork/") + id;
}
inline std::string heroPath(TitleId id)     { return artworkDir(id) + "/hero.img"; }
inline std::string logoPath(TitleId id)     { return artworkDir(id) + "/logo.img"; }
inline std::string iconPath(TitleId id)     { return artworkDir(id) + "/icon.img"; }
inline std::string metadataPath(TitleId id) { return artworkDir(id) + "/metadata.json"; }

// A metadata.json is "current" if it records the same matcher version or if
// the user manually selected an image (manual selection is never evicted).
struct CachedMetadata {
    int     matcherVersion{0};
    bool    manuallySelected{false};  // selectedHeroId / selectedLogoId present
    bool    noArtwork{false};         // SteamGridDB confirmed no artwork
    long long steamGridDbGameId{0};

    bool isCurrent() const {
        return manuallySelected || matcherVersion == kMatcherVersion;
    }
};

// ---- One title's cover state ----------------------------------------------

struct TitleCoverState {
    TitleId       titleId{0};
    CoverStatus   status{CoverStatus::Idle};
    ArtworkSource source{ArtworkSource::None};
    ArtworkKind   kind{ArtworkKind::Hero};

    // Set only when status == Ready.
    std::string   localPath;      // absolute path to the decoded image on SD

    // Set when status == Error or when the transition emits a user message.
    std::string   errorMessage;

    // Monotonically increasing; bumped on every state change.
    // The render thread skips redecoding if the generation matches what it has.
    std::uint32_t generation{0};

    // Set when status == Ambiguous and a picker is needed.
    // Each entry: { gameId, gameName, matchScore } — maximum 12 (HomeNX cap).
    struct Candidate {
        long long   gameId{0};
        std::string name;
        float       score{0.f};
    };
    std::vector<Candidate> candidates;

    // ---- State transitions (called from the network layer) ----------------

    void setPending() {
        status = CoverStatus::Pending;
        ++generation;
    }

    void setLoading() {
        status = CoverStatus::Loading;
        ++generation;
    }

    void setReady(ArtworkSource src, const std::string& path) {
        status = CoverStatus::Ready;
        source = src;
        localPath = path;
        candidates.clear();
        errorMessage.clear();
        ++generation;
    }

    void setNoKey() {
        // Skip SteamGridDB: re-enter the chain at GameTDB.
        status   = CoverStatus::NoKey;
        source   = ArtworkSource::None;
        ++generation;
    }

    void setNoCover() {
        status   = CoverStatus::NoCover;
        source   = ArtworkSource::None;
        ++generation;
    }

    void setAmbiguous(std::vector<Candidate> matches) {
        status     = CoverStatus::Ambiguous;
        source     = ArtworkSource::None;
        candidates = std::move(matches);
        ++generation;
    }

    void setError(const std::string& message) {
        status       = CoverStatus::Error;
        source       = ArtworkSource::None;
        errorMessage = message;
        ++generation;
    }

    // ---- Presentation helpers (render thread) -----------------------------

    // True when the tile may start showing the artwork immediately (no spinner).
    bool isReady() const { return status == CoverStatus::Ready; }

    // True when a spinner should run.
    bool isLoading() const {
        return status == CoverStatus::Pending || status == CoverStatus::Loading;
    }

    // True when a user action is blocked (e.g. the picker button is locked).
    bool isTerminal() const {
        return status == CoverStatus::Ready
            || status == CoverStatus::Error
            || status == CoverStatus::NoCover;
    }

    // The source label for the Settings > Look picker, matching HomeNX copy.
    const char* sourceName() const {
        switch (source) {
        case ArtworkSource::SteamGridDb: return "SteamGridDB";
        case ArtworkSource::GameTdb:     return "GameTDB";
        case ArtworkSource::EshopBanner: return "eShop";
        default:                         return "—";
        }
    }
};

// ---- Store ----------------------------------------------------------------
//
// Owns the per-title states; driven by the loading pipeline and read by the
// render loop.  Thread-safety contract: mutators are called only from the
// single background worker; const getters are safe from the render thread.

class CoverStore final {
public:
    // Returns true when the cover for `titleId` should be fetched.
    // The caller owns the scheduling; this just answers whether the work is
    // still outstanding.
    bool needsFetch(TitleId titleId) const {
        const auto it = m_states.find(titleId);
        if (it == m_states.end()) return true;
        const auto& s = it->second;
        return s.status == CoverStatus::Idle
            || s.status == CoverStatus::NoKey  // escalate to GameTDB
            || s.status == CoverStatus::NoCover;
    }

    // Returns the current state; caller gets a copy, never a dangling reference.
    TitleCoverState get(TitleId titleId) const {
        const auto it = m_states.find(titleId);
        if (it != m_states.end()) return it->second;
        TitleCoverState s;
        s.titleId = titleId;
        return s;
    }

    // ---- Mutators (background worker only) --------------------------------

    TitleCoverState& mutableGet(TitleId titleId) {
        auto [it, inserted] = m_states.emplace(titleId, TitleCoverState{});
        if (inserted) it->second.titleId = titleId;
        return it->second;
    }

    void erase(TitleId titleId) { m_states.erase(titleId); }

    // Invalidates every title that is not manually-selected, so the next
    // tick re-fetches from the network. Called when the API key changes.
    void invalidateAutoMatched() {
        for (auto& [id, state] : m_states) {
            if (state.source != ArtworkSource::SteamGridDb) continue;
            // Keep manually-selected entries; they never need a re-match.
            state.status = CoverStatus::Idle;
            state.localPath.clear();
            ++state.generation;
        }
    }

private:
    std::map<TitleId, TitleCoverState> m_states;
};

// ---- Fallback-chain decision logic ----------------------------------------
//
// Given what we know about the current state, what should the pipeline do next?
// Called from the worker after each step fails or is skipped.

enum class FallbackAction : unsigned char {
    TrySteamGridDb,   // have a key, nothing cached: query the API
    TryGameTdb,       // key absent/failed or no SteamGridDB art
    TryEshopBanner,   // GameTDB also failed: use the system icon
    Done,             // already ready or error
    NeedUserKey,      // SteamGridDB step desired but key not configured
    NeedUserPick,     // SteamGridDB found multiple matches: prompt the picker
};

struct FallbackContext {
    bool hasSteamGridDbKey{false};
    bool cachedArtworkPresent{false};
    bool cacheIsCurrent{false};   // matcherVersion matches or manually selected
};

inline FallbackAction decideFallback(const TitleCoverState& state,
                                     const FallbackContext& ctx) {
    if (state.status == CoverStatus::Ready || state.status == CoverStatus::Error)
        return FallbackAction::Done;

    if (state.status == CoverStatus::Ambiguous)
        return FallbackAction::NeedUserPick;

    // If a valid cache hit exists (and the matcher is current) we just load it.
    if (ctx.cachedArtworkPresent && ctx.cacheIsCurrent)
        return FallbackAction::Done;

    // SteamGridDB first — only when a key is configured.
    if (ctx.hasSteamGridDbKey) {
        if (state.status == CoverStatus::NoCover) {
            // SteamGridDB confirmed nothing: skip to GameTDB.
        } else {
            if (!ctx.hasSteamGridDbKey)
                return FallbackAction::NeedUserKey;
            if (state.status == CoverStatus::Idle
                || state.status == CoverStatus::Pending
                || state.status == CoverStatus::Loading)
                return FallbackAction::TrySteamGridDb;
            if (state.status == CoverStatus::NoKey)
                return FallbackAction::NeedUserKey;
        }
    }

    // GameTDB second.
    if (state.status == CoverStatus::NoCover
        || state.status == CoverStatus::NoKey
        || (!ctx.hasSteamGridDbKey && state.status != CoverStatus::Ready))
        return FallbackAction::TryGameTdb;

    // Last resort: eShop banner (always available).
    return FallbackAction::TryEshopBanner;
}

} // namespace desknx::artwork
