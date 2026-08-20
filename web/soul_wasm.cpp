// soul_wasm.cpp — the doodleink engine + doodlesoul identity, compiled to
// WebAssembly for the web verifier. Shares soul.h with the firmware, so
// the browser computes the same soul from the same MAC by construction.
//
// build: see build.sh
#include <emscripten/emscripten.h>
#include <string.h>
#include <stdio.h>
#include "doodleink.h"
#include "soul.h"

static const int W = 270, H = 480;   // 2x the M5StickC Plus panel
static uint8_t rgba[W * H * 4];
static uint8_t paper[W * H * 4];
static uint8_t cov[W * H];
static uint32_t paperSeed = 0;
static int paperSkin = -1;
static uint32_t traitSeed = 0;
static dd::FaceTraits baseTraits;
static char nameBuf[16];
static char cardBuf[768];

struct RgbaCanvas : dd::Canvas {
  uint8_t* px = rgba;
  int width() const override { return W; }
  int height() const override { return H; }
  void blend(int x, int y, const uint8_t rgb[3], float a) override {
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    uint8_t* p = px + ((size_t)y * W + x) * 4;
    for (int i = 0; i < 3; i++) p[i] = (uint8_t)(p[i] + (rgb[i] - p[i]) * a + 0.5f);
    p[3] = 255;
  }
};
static RgbaCanvas canvas;

static void ensureSoul(uint32_t seed, int skin) {
  if (traitSeed != seed) {
    dd::rollFace(baseTraits, seed);
    traitSeed = seed;
  }
  if (paperSeed != seed || paperSkin != skin) {  // each skin makes its own paper
    canvas.px = paper;
    dd::DD_SKINS[skin].paper(canvas, seed * 7u + 3u);
    canvas.px = rgba;
    paperSeed = seed;
    paperSkin = skin;
  }
}

extern "C" {

// Canonical MAC bytes (aa:bb:cc:dd:ee:ff order) -> the same uint64 that
// ESP.getEfuseMac() returns on the device (byte 0 in the low position).
EMSCRIPTEN_KEEPALIVE
uint32_t soul_from_mac(const uint8_t* mac6) {
  uint64_t v = 0;
  for (int i = 0; i < 6; i++) v |= (uint64_t)mac6[i] << (8 * i);
  return deviceSoulFromMac(v);
}

EMSCRIPTEN_KEEPALIVE
const char* soul_name(uint32_t seed) {
  soulName(seed, nameBuf, sizeof nameBuf);
  return nameBuf;
}

EMSCRIPTEN_KEEPALIVE int skin_count() { return dd::skinCount(); }
EMSCRIPTEN_KEEPALIVE const char* skin_name(int k) { return dd::DD_SKINS[k].name; }

EMSCRIPTEN_KEEPALIVE
const char* soul_card(uint32_t seed) {
  ensureSoul(seed, 0);
  int n = snprintf(cardBuf, sizeof cardBuf, "%s soul, %.1f bits\n",
                   dd::tierName(baseTraits.score), baseTraits.score);
  for (int i = 1; i < dd::C_COUNT; i++)  // skip facing (pose, not identity)
    n += snprintf(cardBuf + n, sizeof cardBuf - n, "%s: %s (%.1f%%)\n",
                  dd::CAT_LABELS[i], dd::traitName(baseTraits, i), baseTraits.pct[i] * 100);
  return cardBuf;
}

EMSCRIPTEN_KEEPALIVE uint8_t* frame_buf() { return rgba; }
EMSCRIPTEN_KEEPALIVE int frame_w() { return W; }
EMSCRIPTEN_KEEPALIVE int frame_h() { return H; }

// Same contract as the firmware's drawLive: any skin from the registry,
// held in a pose. skin 0 = doodle, 1 = wild — dd::DD_SKINS order.
EMSCRIPTEN_KEEPALIVE
void render(uint32_t seed, int skin, float turn, float pitch, float roll,
            float gazeX, int mood, uint32_t moodSeed, uint32_t strokeSeed,
            int blink, int speed) {
  if (skin < 0 || skin >= dd::skinCount()) skin = 0;
  ensureSoul(seed, skin);
  dd::SkinPose pose;
  pose.turn = dd::clampf(turn, -0.9f, 0.9f);
  pose.pitch = dd::clampf(pitch, -0.4f, 0.45f);
  pose.roll = dd::clampf(roll, -0.25f, 0.25f);
  pose.gazeX = dd::clampf(gazeX, -0.5f, 0.5f);
  pose.mood = mood;
  pose.moodSeed = moodSeed;
  pose.blink = blink != 0;
  memcpy(rgba, paper, sizeof rgba);
  dd::DD_SKINS[skin].posed(canvas, cov, seed, W * 0.5f, H * 0.46f,
                           (float)H * 0.30f, strokeSeed, speed, pose);
}

}  // extern "C"
