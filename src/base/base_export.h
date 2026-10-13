/**
 * @file base_export.h
 * @author lipingan (lipingan.dev@outlook.com)
 * @brief Symbol export macros for component builds (Chromium style).
 */

#pragma once

#ifndef CTS_BASE_BASE_EXPORT_H_
#define CTS_BASE_BASE_EXPORT_H_

#if defined(CTS_COMPONENT_BUILD)
#if defined(BASE_IMPLEMENTATION)
#define CTS_BASE_EXPORT __attribute__((visibility("default")))
#else
#define CTS_BASE_EXPORT
#endif
#else
#define CTS_BASE_EXPORT
#endif

#endif  // CTS_BASE_BASE_EXPORT_H_
