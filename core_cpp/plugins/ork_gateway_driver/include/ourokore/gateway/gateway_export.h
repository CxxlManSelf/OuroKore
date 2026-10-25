#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(OUROKORE_GATEWAY_EXPORTS)
        #define ORK_GATEWAY_API __declspec(dllexport)
    #else
        #define ORK_GATEWAY_API __declspec(dllimport)
    #endif
#else
    #if defined(OUROKORE_GATEWAY_EXPORTS)
        #define ORK_GATEWAY_API __attribute__((visibility("default")))
    #else
        #define ORK_GATEWAY_API
    #endif
#endif
