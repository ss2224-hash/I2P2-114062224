#pragma once
#include <cstdio>
#include <cstdarg>
#include <cstdlib>

inline bool _log_debug_enabled(){
    static int inited = 0;
    static bool enabled = false;
    if(!inited){
        const char* e = std::getenv("UDEBUG");
        enabled = (e && e[0]);
        inited = 1;
    }
    return enabled;
}

inline void log_debug(const char* fmt, ...){
    if(!_log_debug_enabled()) return;
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
