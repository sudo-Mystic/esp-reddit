#pragma once
// Clock abstraction. Token expiry and rate limiting need "now", but calling
// millis() directly makes the logic untestable on the host. All logic in
// this library goes through reddit::nowMs().
#include <stdint.h>

namespace reddit {

using ClockFn = uint32_t (*)();

// Override the clock (unit tests). Pass nullptr to restore the default
// (millis() on Arduino, steady_clock on host).
void setClock(ClockFn fn);

// Milliseconds since an arbitrary epoch (wraps ~49 days, like millis()).
uint32_t nowMs();

// Whole seconds elapsed since earlierMs, wrap-safe.
inline uint32_t secondsSince(uint32_t earlierMs) {
  return (nowMs() - earlierMs) / 1000;
}

}  // namespace reddit
