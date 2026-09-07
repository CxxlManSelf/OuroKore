#pragma once

/**
 * @file export.h
 * @brief 跨平台符號匯出與呼叫慣例定義
 */

#if defined(_WIN32) || defined(__WIN32__) || defined(WIN32)
  #define ORK_DECL_EXPORT __declspec(dllexport)
  #define ORK_DECL_IMPORT __declspec(dllimport)
  #define ORK_CALL        __cdecl
#else
  #define ORK_DECL_EXPORT __attribute__((visibility("default")))
  #define ORK_DECL_IMPORT __attribute__((visibility("default")))
  #define ORK_CALL
#endif

// Base 模組符號匯出/匯入巨集
#if defined(OUROKORE_BASE_EXPORTS)
  #define ORK_BASE_API ORK_DECL_EXPORT
#else
  #define ORK_BASE_API ORK_DECL_IMPORT
#endif
