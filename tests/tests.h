// Copyright (c) 2017 AlertAvert.com. All rights reserved.
// Created by M. Massenzio (marco@alertavert.com) on 4/15/17.

#pragma once

#include <chrono>
#include <functional>
#include <random>
#include <thread>

namespace tests {

static const unsigned short kMinPort = 10000;
static const unsigned short kMaxPort = 50000;

static bool seeded = false;

/**
 * Random number generator engine; using the default for the system, it will be
 * seeded during static initialization and the sequence will be used by every
 * test.
 */
static std::default_random_engine DRE;

/**
 * randomized port numbers to be used during tests.
 */
static std::vector<unsigned short> ports;
/**
 * called once by the `RandomPort()` method.
 */
inline void _init_ports() {
  seeded = true;
  ports = std::vector<unsigned short>(kMaxPort - kMinPort + 1);
  std::iota(std::begin(ports), std::end(ports), kMinPort);
  std::shuffle(std::begin(ports), std::end(ports),
               std::mt19937{std::random_device{}()});
}

/**
 * @return a random integer, to be used as port number during tests.
 */
inline unsigned short RandomPort() {
  if (!seeded) {
    _init_ports();
  }

  auto ret = ports.back();
  ports.pop_back();

  return ret;
}

/**
 * Helper method to wait at most `timeout` msec for `pred` to become `true`; but
 * hopefully a lot less.
 *
 * <p>We slice the total timeout in "slices" of `sliceDuration` msec, and test
 * the predicate every time until it either becomes true or we run out of time.
 *
 * <p>Assumes that the predicate tested is idempotent (in other words, the fact
 * that we test it several times instead of only once will not impact the
 * outcome) and that it does change over time (typically, because another thread
 * is busy doing something).
 *
 * @param pred a predicate function whose outcome we expect to become `true`
 * @param timeout the amount of time we are willing to wait, at most
 * @param sliceDuration how often to test the predicate; by default, 100 msec,
 * should be a fraction of `timeout`; we will test `pred` at least once, if not.
 * @return whether `pred` outcome became true before `timeout` ran out, as soon
 * as it does, with `sliceDuration` accuracy
 */
inline bool WaitAtMostFor(
    const std::function<bool()> &pred, std::chrono::milliseconds timeout,
    std::chrono::milliseconds sliceDuration = std::chrono::milliseconds(100)) {
  long retries = timeout / sliceDuration;

  // If the caller is not willing to wait at least one "slice" we'll just test
  // the predicate.
  if (retries < 1) {
    return pred();
  }

  while (!pred() && retries-- > 0) {
    std::this_thread::sleep_for(sliceDuration);
  }
  return retries > 0;
}
} // namespace tests
