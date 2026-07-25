#pragma once

#include <stdbool.h>

// Connect to WiFi (blocking, with timeout). Returns true on success.
bool net_connect(void);

// Sync clock via SNTP (blocking, with timeout) and set local TZ.
// Returns true if system time is now valid.
bool net_sync_time(void);

// Disconnect WiFi and release resources (radio off before deep sleep).
void net_disconnect(void);
