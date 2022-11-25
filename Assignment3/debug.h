#pragma once

#include <cstdio>

static inline void stderrCyanBackground() {
    fprintf(stderr, "\033[1;1;46m");
}

static inline void stderrNormalBackground() {
    fprintf(stderr, "\033[0m");
}