#pragma once
#include <nextendo/snapshot.hpp>
#include <atomic>
namespace nextendo {
bool initializeNetwork();
void shutdownNetwork();
bool fetchSnapshot(PlayerSnapshot& snapshot, const std::atomic<bool>* running = nullptr);
void resolveInstalledNames(PlayerSnapshot& snapshot, bool nsReady);
} // namespace nextendo
