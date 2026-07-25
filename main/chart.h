#pragma once

#include <time.h>

#include "tides.h"

// Render the full-screen tide chart (header, 24h curve, extremes, axes)
// into the framebuffer. `now` places the current-time marker.
void chart_render(const tide_data_t *tides, time_t now);
