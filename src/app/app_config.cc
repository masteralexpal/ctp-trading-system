/**
 * @file app_config.cc
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief App config + Inject/Get global access.
 */
#include "app/app_config.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <utility>

namespace cts::app {
namespace {

std::optional<AppConfig>& Storage() {
  static std::optional<AppConfig> instance;
  return instance;
}

}  // namespace

void InstallAppConfig(AppConfig config) {
  Storage() = std::move(config);
}

AppConfig const& GetAppConfig() {
  auto& storage = Storage();
  if (!storage) {
    std::cerr << "GetAppConfig() called before InstallAppConfig()\n";
    std::abort();
  }
  return *storage;
}

}  // namespace cts::app
