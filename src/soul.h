// soul.h — the identity of a device, shared verbatim between the firmware
// and the web verifier (compiled to WebAssembly). DO NOT change these
// functions: altering deviceSoulFromMac would kill every soul in the world.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "dd_core.h"

// The soul is the device: a hash of the factory-burned MAC (only ~24 of
// its low bits vary between chips, so mix all 48 through splitmix64).
static inline uint32_t deviceSoulFromMac(uint64_t mac) {
  uint64_t x = mac + 0x9E3779B97F4A7C15ull;
  x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 27; x *= 0x94D049BB133111EBull;
  x ^= x >> 31;
  uint32_t s = (uint32_t)(x ^ (x >> 32));
  return s ? s : 1;
}

// A pronounceable little name, deterministic from the soul.
static inline void soulName(uint32_t seed, char* out, size_t cap) {
  static const char* syl[] = { "ba","be","bo","bu","da","de","do","du","fa","fi","fo",
                               "ga","go","gu","ka","ke","ki","ko","la","le","li","lo","lu",
                               "ma","me","mi","mo","mu","na","ne","ni","no","nu","pa","pe",
                               "pi","po","pu","ra","re","ri","ro","ru","sa","se","si","so",
                               "su","ta","te","ti","to","tu","va","vi","vo","za","zi","zo" };
  const int NS = sizeof(syl) / sizeof(syl[0]);
  dd::Rng r(seed ^ 0x5001AAu);
  int n = r.chance(0.6f) ? 2 : 3;
  size_t p = 0;
  for (int i = 0; i < n && p + 3 < cap; i++) {
    const char* s = syl[r.ri(0, NS - 1)];
    out[p++] = i == 0 ? (char)(s[0] - 32) : s[0];
    out[p++] = s[1];
  }
  if (r.chance(0.3f) && p + 1 < cap) out[p++] = "nrmstk"[r.ri(0, 5)];
  out[p] = 0;
}
