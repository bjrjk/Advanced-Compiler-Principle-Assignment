#ifndef ASSIGN2_UTIL_H
#define ASSIGN2_UTIL_H

#include <algorithm>
#include <vector>

template<class T>
inline static bool add_if_not_exist(std::vector<T> &sequence, const T &value) {
    if (!std::count(sequence.begin(), sequence.end(), value)) {
        sequence.push_back(value);
        return true;
    }
    return false;
}

#endif
