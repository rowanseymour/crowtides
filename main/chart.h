#pragma once

#include <time.h>

#include "tides.h"
#include "weather.h"

// Render one day's tide chart into the framebuffer: the day starting at
// local midnight `day_start`, sliced from the (multi-day) tide data.
// `day_offset` is the offset from today, shown as a relative label under
// the date when non-zero. `weather` may have no entry for `day_start`
// (forecast horizon is much shorter than the tide cache) — the hi/lo/rain
// line is simply omitted when so.
void chart_render(const tide_data_t *tides, const weather_data_t *weather,
                  time_t day_start, int day_offset);
