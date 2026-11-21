#include "SteamGridDbService.hpp"
#include "NetworkRuntime.hpp"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
namespace desknx::steamgriddb {
namespace { constexpr std::size_t MaxQueuedJobs=8; }
SteamGridDbService::~SteamGridDbService(){shutdown();}
bool SteamGridDbService::ensureWorker(){
 if(worker_)return true;
 if(!mutex_)mutex_=SDL_CreateMutex();
 if(!condition_)condition_=SDL_CreateCond();
 if(!mutex_||!condition_){if(condition_)SDL_DestroyCond(condition_);if(mutex_)SDL_DestroyMutex(mutex_);condition_=nullptr;mutex_=nullptr;return false;}
 SDL_AtomicSet(&stopRequested_,0);
 stopWorker_=false;
 worker_=SDL_CreateThread(&SteamGridDbService::workerEntry,"DeskNX SteamGridDB",this);
 if(!worker_){SDL_DestroyCond(condition_);SDL_DestroyMutex(mutex_);condition_=nullptr;mutex_=nullptr;return false;}
 return true;
}
int SDLCALL SteamGridDbService::workerEntry(void* p){return static_cast<SteamGridDbService*>(p)->workerLoop();}
bool SteamGridDbService::cancelNetwork(void* context){
 auto* self=static_cast<SteamGridDbService*>(context);
 return !self||SDL_AtomicGet(&self->stopRequested_)!=0;
}
bool SteamGridDbService::enqueue(Job job){
 if(!mutex_)return false;
 SDL_LockMutex(mutex_);
 if(stopWorker_){SDL_UnlockMutex(mutex_);return false;}
 // Rapid controller/touch movement should preview only the latest highlighted
 // image; stale queued preview requests add latency without user value.
 if(job.type==JobType::Preview) {
  for(auto it=jobs_.begin();it!=jobs_.end();) {
   if(it->type==JobType::Preview&&it->revision==job.revision)
    it=jobs_.erase(it);
   else ++it;
  }
 }
 if(jobs_.size()>=MaxQueuedJobs){SDL_UnlockMutex(mutex_);return false;}
 jobs_.push_back(std::move(job));
 SDL_CondSignal(condition_);
 SDL_UnlockMutex(mutex_);
 return true;
}
bool SteamGridDbService::search(std::uint64_t id,const std::string& title,
        const std::string& key,bool useSavedMapping){
 if(id==0||title.empty()||key.empty()||!ensureWorker())return false;
 Job job;job.type=JobType::Search;job.titleId=id;job.title=title;job.apiKey=key;
 job.useSavedMapping=useSavedMapping;
 SDL_LockMutex(mutex_);++revision_;job.revision=revision_;
 state_=ServiceSnapshot{};state_.status=ServiceStatus::Searching;state_.message="Searching SteamGridDB...";
 state_.titleId=id;state_.title=title;state_.kind=ArtworkKind::Grid;state_.revision=revision_;
 jobs_.erase(std::remove_if(jobs_.begin(),jobs_.end(),
     [](const Job& queued){return queued.type!=JobType::Cleanup;}),jobs_.end());
 SDL_UnlockMutex(mutex_);
 if(!enqueue(std::move(job))){publishError(revision_,"SteamGridDB worker could not queue request");return false;}
 return true;
}
bool SteamGridDbService::loadAssets(std::uint64_t id,std::uint64_t gameId,const std::string& title,
 const std::string& gameName,ArtworkKind kind,const std::string& key){
 if(id==0||gameId==0||key.empty()||!ensureWorker())return false;
 Job job;job.type=JobType::Assets;job.titleId=id;job.gameId=gameId;job.title=title;job.gameName=gameName;job.kind=kind;job.apiKey=key;
 SDL_LockMutex(mutex_);++revision_;job.revision=revision_;
 state_.status=ServiceStatus::LoadingAssets;state_.message=std::string("Loading ")+artworkKindName(kind)+" artwork...";
 state_.titleId=id;state_.gameId=gameId;state_.title=title;state_.gameName=gameName;state_.kind=kind;
 state_.games.clear();state_.assets.clear();state_.selectedAsset=0;state_.previewAsset=-1;state_.previewPath.clear();
 state_.previewRevision=0;state_.revision=revision_;
 jobs_.erase(std::remove_if(jobs_.begin(),jobs_.end(),
     [](const Job& queued){return queued.type!=JobType::Cleanup;}),jobs_.end());SDL_UnlockMutex(mutex_);
 if(!enqueue(std::move(job))){publishError(revision_,"SteamGridDB could not queue artwork request");return false;}return true;
}
bool SteamGridDbService::selectAsset(int index){
 if(!mutex_)return false;Job job;SDL_LockMutex(mutex_);
 if(state_.status!=ServiceStatus::Browsing||index<0||index>=static_cast<int>(state_.assets.size())){
  SDL_UnlockMutex(mutex_);return false;}
 if(state_.selectedAsset==index&&state_.previewAsset==index){SDL_UnlockMutex(mutex_);return true;}
 state_.selectedAsset=index;state_.message="Loading preview...";job.type=JobType::Preview;job.revision=revision_;
 job.titleId=state_.titleId;job.assetIndex=index;job.asset=state_.assets[static_cast<size_t>(index)];
 ++state_.revision;SDL_UnlockMutex(mutex_);return enqueue(std::move(job));
}
bool SteamGridDbService::pruneCache(std::uint64_t maximumBytes,
        const std::vector<std::uint64_t>& installedTitleIds) {
 if(!ensureWorker())return false;
 Job job;
 job.type=JobType::Cleanup;
 job.maximumCacheBytes=maximumBytes;
 job.installedTitleIds=installedTitleIds;
 return enqueue(std::move(job));
}
bool SteamGridDbService::applySelected(){
 if(!mutex_)return false;Job job;SDL_LockMutex(mutex_);
 if(state_.status!=ServiceStatus::Browsing||state_.selectedAsset<0||
    state_.selectedAsset>=static_cast<int>(state_.assets.size())){SDL_UnlockMutex(mutex_);return false;}
 job.type=JobType::Apply;job.revision=revision_;job.titleId=state_.titleId;job.gameId=state_.gameId;
 job.kind=state_.kind;job.assetIndex=state_.selectedAsset;job.asset=state_.assets[static_cast<size_t>(state_.selectedAsset)];
 job.title=state_.title;job.gameName=state_.gameName;state_.status=ServiceStatus::Applying;
 state_.message="Downloading selected artwork...";++state_.revision;SDL_UnlockMutex(mutex_);
 if(!enqueue(std::move(job))){publishError(revision_,"SteamGridDB apply queue is busy");return false;}return true;
}
ServiceSnapshot SteamGridDbService::snapshot()const{
 if(!mutex_)return state_;SDL_LockMutex(mutex_);ServiceSnapshot s=state_;SDL_UnlockMutex(mutex_);return s;
}
std::string SteamGridDbService::previewPath(std::uint64_t titleId,std::uint64_t assetId)const{
 return std::string(SteamGridDbCache::kCacheRoot)+"/.preview-"+
     std::to_string(titleId)+"-"+std::to_string(assetId)+".tmp";
}
void SteamGridDbService::publishError(std::uint64_t rev,const std::string& message){
 if(!mutex_)return;SDL_LockMutex(mutex_);if(rev==revision_){state_.status=ServiceStatus::Error;state_.message=message;++state_.revision;}SDL_UnlockMutex(mutex_);
}
void SteamGridDbService::publishPreview(std::uint64_t rev,int index,const std::string& path){
 if(!mutex_)return;SDL_LockMutex(mutex_);
 if(rev==revision_&&state_.selectedAsset==index&&index>=0&&index<static_cast<int>(state_.assets.size())){
  state_.previewPath=path;state_.previewAsset=index;state_.status=ServiceStatus::Browsing;
  state_.message="Choose artwork, then press A to apply";++state_.previewRevision;++state_.revision;
 }SDL_UnlockMutex(mutex_);
}
int SteamGridDbService::workerLoop(){
 SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
 for(;;){
  Job j;SDL_LockMutex(mutex_);while(!stopWorker_&&jobs_.empty())SDL_CondWait(condition_,mutex_);
  if(stopWorker_){SDL_UnlockMutex(mutex_);break;}j=std::move(jobs_.front());jobs_.pop_front();SDL_UnlockMutex(mutex_);
  if(j.type==JobType::Cleanup) {
   (void)cache_.prune(j.maximumCacheBytes,j.installedTitleIds);
   continue;
  }
  if(!networkReady_)networkReady_=desknx::network::acquire();
  if(!networkReady_){publishError(j.revision,"Network service unavailable; check Wi-Fi");continue;}
  std::string error;
  if(j.type==JobType::Search){
   const std::uint64_t mapped=j.useSavedMapping?cache_.mappedGameId(j.titleId):0;
   if(mapped) {
    Job mappedJob=j;
    mappedJob.type=JobType::Assets;
    mappedJob.gameId=mapped;
    mappedJob.gameName=j.title;
    mappedJob.kind=ArtworkKind::Grid;
    mappedJob.mappedLookup=true;
    SDL_LockMutex(mutex_);
    if(j.revision==revision_) {
     state_.status=ServiceStatus::LoadingAssets;
     state_.gameId=mapped;
     state_.gameName=j.title;
     state_.message="Loading saved SteamGridDB game mapping...";
     jobs_.push_front(std::move(mappedJob));
     ++state_.revision;
    }
    SDL_UnlockMutex(mutex_);
    continue;
   }
   std::vector<GameResult> games;
   if(!searchGames(j.apiKey,j.title,games,error,&SteamGridDbService::cancelNetwork,this)){publishError(j.revision,error);continue;}
   SDL_LockMutex(mutex_);
   if(j.revision==revision_){
    state_.games=std::move(games);
    state_.status=ServiceStatus::MatchesReady;
    state_.message=state_.games.empty()?"No SteamGridDB matches found":"Choose the matching SteamGridDB entry";
    ++state_.revision;
   }
   SDL_UnlockMutex(mutex_);
   continue;
  }
  if(j.type==JobType::Assets){
   std::vector<ArtworkAsset> assets;
   if(!fetchAssets(j.apiKey,j.gameId,j.kind,assets,error,&SteamGridDbService::cancelNetwork,this)) {
    if(j.mappedLookup) {
     // Saved SGDB mappings can become stale; fall back to a fresh title search.
     std::vector<GameResult> games;
     std::string searchError;
     if(searchGames(j.apiKey,j.title,games,searchError,&SteamGridDbService::cancelNetwork,this)) {
      SDL_LockMutex(mutex_);
      if(j.revision==revision_) {
       state_.games=std::move(games);
       state_.gameId=0;
       state_.gameName.clear();
       state_.status=ServiceStatus::MatchesReady;
       state_.message=state_.games.empty()?
          "Saved mapping expired; no title matches found":
          "Saved mapping expired; choose the correct SteamGridDB game";
       ++state_.revision;
      }
      SDL_UnlockMutex(mutex_);
     } else publishError(j.revision,searchError);
    } else publishError(j.revision,error);
    continue;
   }
   SDL_LockMutex(mutex_);bool current=j.revision==revision_;
   if(current){state_.gameId=j.gameId;state_.gameName=j.gameName;state_.kind=j.kind;state_.assets=std::move(assets);
    state_.selectedAsset=0;state_.previewAsset=-1;state_.previewPath.clear();state_.status=ServiceStatus::Browsing;
    state_.message=state_.assets.empty()?"No static PNG/JPEG artwork found for this type":"Artwork list loaded";++state_.revision;}
   bool has=current&&!state_.assets.empty();ArtworkAsset first;if(has)first=state_.assets.front();SDL_UnlockMutex(mutex_);
   if(current)cache_.saveGameMapping(j.titleId,j.gameId,j.gameName.empty()?j.title:j.gameName);
   if(has){cache_.ensureDirectories();const std::string path=previewPath(j.titleId,first.id);
    if(downloadImage(first.thumb,path,error,&SteamGridDbService::cancelNetwork,this))publishPreview(j.revision,0,path);
    else{SDL_LockMutex(mutex_);if(j.revision==revision_){state_.message="Preview unavailable; full image can still be applied";++state_.revision;}SDL_UnlockMutex(mutex_);}}
   continue;
  }
  if(j.type==JobType::Preview){
   cache_.ensureDirectories();const std::string path=previewPath(j.titleId,j.asset.id);
   if(!downloadImage(j.asset.thumb,path,error,&SteamGridDbService::cancelNetwork,this)){publishError(j.revision,error);continue;}
   publishPreview(j.revision,j.assetIndex,path);continue;
  }
  if(j.type==JobType::Apply){
   cache_.ensureDirectories();const std::string temp=std::string(SteamGridDbCache::kCacheRoot)+"/.download.tmp";
   if(!downloadImage(j.asset.url,temp,error,&SteamGridDbService::cancelNetwork,this)){publishError(j.revision,error);continue;}
   bool saved=cache_.saveArtworkFromFile(j.titleId,
    static_cast<SteamGridDbCache::ArtworkKind>(j.kind),temp,
    j.gameId,j.asset.id,j.gameName,true);
   std::remove(temp.c_str());SDL_LockMutex(mutex_);
   if(j.revision==revision_){state_.status=saved?ServiceStatus::Applied:ServiceStatus::Error;
    state_.message=saved?"Artwork saved to the SD card":"Downloaded image could not be committed to cache";++state_.revision;}
   SDL_UnlockMutex(mutex_);
  }
 }
 if(networkReady_){desknx::network::release();networkReady_=false;}return 0;
}
void SteamGridDbService::shutdown(){
 SDL_AtomicSet(&stopRequested_,1);
 if(mutex_){SDL_LockMutex(mutex_);stopWorker_=true;jobs_.clear();SDL_CondBroadcast(condition_);SDL_UnlockMutex(mutex_);}
 if(worker_){SDL_WaitThread(worker_,nullptr);worker_=nullptr;}
 if(condition_){SDL_DestroyCond(condition_);condition_=nullptr;}
 if(mutex_){SDL_DestroyMutex(mutex_);mutex_=nullptr;}
 jobs_.clear();
 std::error_code cleanupError;
 for(std::filesystem::directory_iterator it(SteamGridDbCache::kCacheRoot,cleanupError),end;
     !cleanupError&&it!=end;it.increment(cleanupError)) {
  const std::string name=it->path().filename().string();
  if(name.rfind(".preview-",0)==0) {
   std::error_code removeError;
   std::filesystem::remove(it->path(),removeError);
  }
 }
 std::remove((std::string(SteamGridDbCache::kCacheRoot)+"/.preview.tmp").c_str());
}
} // namespace desknx::steamgriddb
