#pragma once
#include <desknx/SteamGridDbClient.hpp>
#include "SteamGridDbCache.hpp"
#include <SDL2/SDL.h>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>
namespace desknx::steamgriddb {
enum class ServiceStatus : unsigned char { Idle,Searching,MatchesReady,LoadingAssets,Browsing,PreviewLoading,Applying,Applied,Error };
struct ServiceSnapshot {
 ServiceStatus status{ServiceStatus::Idle};
 std::string message;
 std::uint64_t titleId{0},gameId{0};
 std::string title,gameName;
 ArtworkKind kind{ArtworkKind::Grid};
 std::vector<GameResult> games;
 std::vector<ArtworkAsset> assets;
 int selectedAsset{0},previewAsset{-1};
 std::string previewPath;
 std::uint64_t previewRevision{0},revision{0};
};
class SteamGridDbService final {
public:
 SteamGridDbService()=default;
 ~SteamGridDbService();
 bool search(std::uint64_t titleId,const std::string& title,const std::string& apiKey,
             bool useSavedMapping=true);
 bool loadAssets(std::uint64_t titleId,std::uint64_t gameId,const std::string& title,
                 const std::string& gameName,ArtworkKind kind,const std::string& apiKey);
 bool selectAsset(int index);
 bool applySelected();
 bool pruneCache(std::uint64_t maximumBytes,const std::vector<std::uint64_t>& installedTitleIds);
 void cancelBrowsing();
 ServiceSnapshot snapshot() const;
 void shutdown();
 SteamGridDbCache& cache(){return cache_;}
 const SteamGridDbCache& cache() const{return cache_;}
private:
 enum class JobType : unsigned char { Search,Assets,Preview,Apply,Cleanup };
 struct Job {
  JobType type{JobType::Search};
  std::uint64_t revision{0},titleId{0},gameId{0};
  ArtworkKind kind{ArtworkKind::Grid};
  int assetIndex{0};
  std::string title,gameName,apiKey;
  std::vector<std::uint64_t> installedTitleIds;
  std::uint64_t maximumCacheBytes{0};
  bool useSavedMapping{true};
  bool mappedLookup{false};
  ArtworkAsset asset;
 };
 struct NetworkCancelContext {
  SteamGridDbService* service{nullptr};
  std::uint64_t revision{0};
 };
 bool ensureWorker();
 static bool cancelNetwork(void* context);
 bool enqueue(Job job);
 static int SDLCALL workerEntry(void* context);
 int workerLoop();
 void publishError(std::uint64_t revision,const std::string& message);
 void publishPreview(std::uint64_t revision,int index,const std::string& path);
 std::string previewPath(std::uint64_t titleId,std::uint64_t assetId) const;
 mutable SDL_mutex* mutex_{nullptr};
 SDL_cond* condition_{nullptr};
 SDL_Thread* worker_{nullptr};
 bool stopWorker_{false},networkReady_{false};
 SDL_atomic_t stopRequested_{};
 std::uint64_t revision_{0};
 std::deque<Job> jobs_;
 ServiceSnapshot state_;
 SteamGridDbCache cache_;
};
} // namespace desknx::steamgriddb
