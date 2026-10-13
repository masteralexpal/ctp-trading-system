/**
 * @file cli.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Command-line parsing entry.
 */
#pragma once

#ifndef CTS_APP_CLI_H_
#define CTS_APP_CLI_H_

#include <optional>

#include "app/app_config.h"

namespace cts::app {

struct CliResult {
  std::optional<AppConfig> config;
  int exit_code{0};
};

CliResult ParseArgs(int argc, char** argv);

}  // namespace cts::app

#endif  // CTS_APP_CLI_H_
