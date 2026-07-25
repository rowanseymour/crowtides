#pragma once

// CrowPanel 5.79" onboard controls. All are active-low, need a pull-up,
// and sit on RTC-capable GPIOs so they can wake the chip from deep sleep.
#define BTN_MENU        2
#define BTN_EXIT        1
#define BTN_WHEEL_UP    6
#define BTN_WHEEL_DOWN  4
#define BTN_WHEEL_CLICK 5
