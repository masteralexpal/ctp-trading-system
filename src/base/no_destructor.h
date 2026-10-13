/**
 * @file no_destructor.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Static object wrapper that is never destructed (kills static
 * destruction order fiasco).
 */
#pragma once

#ifndef CTS_BASE_NO_DESTRUCTOR_H_
#define CTS_BASE_NO_DESTRUCTOR_H_

#include <new>
#include <utility>

namespace cts::base {

template <typename T>
class NoDestructor {
 public:
  template <typename... Args>
  explicit NoDestructor(Args&&... args) {
    new (storage_) T(std::forward<Args>(args)...);
  }

  NoDestructor(NoDestructor const&) = delete;
  NoDestructor& operator=(NoDestructor const&) = delete;

  // trivial dtor: nothing is registered with atexit
  ~NoDestructor() = default;

  T* get() noexcept { return reinterpret_cast<T*>(storage_); }
  T const* get() const noexcept { return reinterpret_cast<T const*>(storage_); }

  T& operator*() noexcept { return *get(); }
  T const& operator*() const noexcept { return *get(); }

  T* operator->() noexcept { return get(); }
  T const* operator->() const noexcept { return get(); }

 private:
  alignas(T) char storage_[sizeof(T)];
};

}  // namespace cts::base

#endif  // CTS_BASE_NO_DESTRUCTOR_H_
