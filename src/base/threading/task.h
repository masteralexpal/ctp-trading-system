/**
 * @file task.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Task: the unit of work posted to a TaskLoop.
 */
#pragma once

#ifndef CTS_BASE_THREADING_TASK_H_
#define CTS_BASE_THREADING_TASK_H_

#include <functional>

namespace cts::base {
using Task = std::move_only_function<void()>;
}  // namespace cts::base

#endif  // CTS_BASE_THREADING_TASK_H_
