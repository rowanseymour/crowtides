#pragma once

#include <time.h>

#include "tides.h"

// Render one day's tide chart into the framebuffer: the day starting at
// local midnight `day_start`, sliced from the (multi-day) tide data.
// `day_offset` is the offset from today, shown as a badge when non-zero.
void chart_render(const tide_data_t *tides, time_t day_start, int day_offset);
