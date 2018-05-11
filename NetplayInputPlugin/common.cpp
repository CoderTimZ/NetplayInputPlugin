#include "stdafx.h"

#include "common.h"

double timestamp() {
    using namespace std::chrono;

    return duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count() / 1000000.0;
}
