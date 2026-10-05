#include "RedditTime.h"

#ifdef ARDUINO
#include <Arduino.h>
static uint32_t defaultClock() { return millis(); }
#else
#include <chrono>
static uint32_t defaultClock() {
  using namespace std::chrono;
  return static_cast<uint32_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
          .count());
}
#endif

namespace reddit {

static ClockFn s_clock = defaultClock;

void setClock(ClockFn fn) { s_clock = fn ? fn : defaultClock; }

uint32_t nowMs() { return s_clock(); }

}  // namespace reddit
