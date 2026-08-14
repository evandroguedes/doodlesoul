// doodlesoul — every device holds ONE unique hand-drawn character,
// derived from the chip's factory-burned MAC address. The soul was always
// latent in the silicon; this firmware just wakes it up.
//
// Reflashing, updating, fixing bugs: the soul survives all of it. Two
// devices flashing the same binary get different souls, guaranteed by
// the hardware. There is no command to change it. That is the point.
//
//   BtnA (front)  press : pet it (it beams and does a little hop)
//   BtnB (side)   click : soul card (name, traits, rarity, boots)
//   BtnB          hold  : freeze / unfreeze (fine-art still)
//   tilt                : it turns to follow gravity, like a level
//   shake               : it gets dizzy (careful)
//   leave it alone      : it daydreams — glances, blinks, mood swings
//
// Serial console: s = screenshot (tools/screenshot.py), i = accel dump.
#include <M5Unified.h>
#include <Preferences.h>
#include <doodleink.h>

SET_LOOP_TASK_STACK_SIZE(24 * 1024);

// ---- IMU response (flip a sign if your unit feels inverted) ----
static const float YAW_SIGN = 1.0f;
static const float PITCH_SIGN = 1.0f;
static const float YAW_GAIN = 3.2f;
static const float PITCH_GAIN = 1.8f;
static const uint32_t FRAME_MS = 80;

// ---- power ----
static const uint8_t BRIGHT_FULL = 180;
static const uint8_t BRIGHT_DIM = 70;
static const uint32_t DIM_AFTER = 120000;
static const uint32_t SLEEP_AFTER = 360000;
static const uint32_t WAKE_GRACE = 1200;
static const uint32_t WAKE_HOLD = 300;

static uint16_t* fb = nullptr;
static uint16_t* paperBuf = nullptr;
static uint8_t* cov = nullptr;
static int W = 135, H = 240;

struct Fb565Canvas : dd::Canvas {
  int width() const override { return W; }
  int height() const override { return H; }
  uint16_t* fb565() override { return fb; }
  void blend(int x, int y, const uint8_t rgb[3], float a) override {
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    uint16_t& d = fb[y * W + x];
    int ia = (int)(a * 256.0f + 0.5f);
    if (ia <= 0) return;
    if (ia > 256) ia = 256;
    int dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
    int sr = rgb[0] >> 3, sg = rgb[1] >> 2, sb = rgb[2] >> 3;
    dr += ((sr - dr) * ia) >> 8;
    dg += ((sg - dg) * ia) >> 8;
    db += ((sb - db) * ia) >> 8;
    d = (uint16_t)((dr << 11) | (dg << 5) | db);
  }
};

static Fb565Canvas canvas;
static Preferences prefs;
static dd::FaceTraits base, live;
static dd::Rng anim(1);

// soul
static uint32_t soulSeed = 1;
static uint32_t boots = 1;
static bool justBorn = false;
static char soulName_[16];

// animation
static float yaw = 0, pitch = 0, roll = 0;
static float idleYawT = 0, idlePitchT = 0, idleRollT = 0;
static float gazeT = 0, gaze = 0, bounce = 0;
static int mood = 0;
static uint32_t moodSeed = 1;
static uint32_t nextGlance = 0, nextMood = 0, nextBlink = 0, blinkUntil = 0;
static uint32_t hopUntil = 0, dizzyUntil = 0, lastDizzy = 0;
static uint32_t frameNo = 0, drawMs = 0;
static bool frozen = false, showCard = false;

// power
static uint32_t lastInteract = 0;
static bool dimmed = false, asleep = false;
static uint32_t sleepSince = 0, movingSince = 0;
static int batPct = 100;
static bool charging = false;
static uint32_t nextBatRead = 0;

// A pronounceable little name, deterministic from the soul.
static void makeName(uint32_t seed, char* out, size_t cap) {
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

// The soul is the device: a hash of the factory-burned MAC (only ~24 of
// its low bits vary between chips, so mix all 48 through splitmix64).
static uint32_t deviceSoul() {
  uint64_t x = ESP.getEfuseMac() + 0x9E3779B97F4A7C15ull;
  x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
  x ^= x >> 27; x *= 0x94D049BB133111EBull;
  x ^= x >> 31;
  uint32_t s = (uint32_t)(x ^ (x >> 32));
  return s ? s : 1;
}

static void loadSoul() {
  prefs.begin("soul", false);
  soulSeed = deviceSoul();
  justBorn = prefs.getUInt("met", 0) != soulSeed;  // first time meeting this life
  if (justBorn) {
    boots = 1;
    prefs.putUInt("met", soulSeed);
    prefs.putUInt("boots", boots);
  } else {
    boots = prefs.getUInt("boots", 0) + 1;
    prefs.putUInt("boots", boots);
  }
  makeName(soulSeed, soulName_, sizeof soulName_);
}

static void printCard() {
  Serial.printf("\n== %s  soul %08x  boot #%u  score %.2f  tier %s  (%u ms/frame)\n",
                soulName_, soulSeed, boots, base.score, dd::tierName(base.score), drawMs);
  for (int i = 0; i < dd::C_COUNT; i++)
    Serial.printf("  %-12s %-10s %.1f%%\n", dd::CAT_LABELS[i], dd::traitName(base, i), base.pct[i] * 100);
}

static void updateBattery() {
  int lvl = M5.Power.getBatteryLevel();
  if (lvl >= 0) batPct = lvl;
  charging = M5.Power.isCharging() == 1;
}

static void drawBatteryDots() {
  const int N = 5;
  const float SEP = 6.5f;
  const float X0 = W - 6 - (N - 1) * SEP;
  const float Y0 = 7;
  int filled = (batPct * N + 99) / 100;
  if (filled > N) filled = N;
  uint8_t inkc[3] = { 74, 70, 62 };
  for (int k = 0; k < N; k++) {
    float r = 1.5f;
    if (charging) {
      float ph = sinf(((millis() + (uint32_t)(k * 250)) % 1400u) / 1400.0f * dd::TAU);
      r = 1.5f + (ph + 1.0f) * 0.45f;
    }
    float a = k < filled ? 0.72f : 0.2f;
    float cx = X0 + k * SEP;
    for (int py = (int)(Y0 - r - 1); py <= (int)(Y0 + r + 1); py++)
      for (int px = (int)(cx - r - 1); px <= (int)(cx + r + 1); px++) {
        float c = r + 0.5f - dd::fastSqrt((px + 0.5f - cx) * (px + 0.5f - cx) + (py + 0.5f - Y0) * (py + 0.5f - Y0));
        if (c <= 0) continue;
        canvas.blend(px, py, inkc, a * (c > 1 ? 1 : c));
      }
  }
}

static void cardOverlay() {
  M5.Display.setTextFont(1);
  int ch = 92;
  M5.Display.fillRect(0, H - ch, W, ch, M5.Display.color565(228, 224, 214));
  M5.Display.drawFastHLine(0, H - ch, W, M5.Display.color565(90, 86, 80));
  M5.Display.setTextColor(M5.Display.color565(44, 42, 38));
  char buf[40];
  int y = H - ch + 4;
  M5.Display.setTextSize(2);
  M5.Display.drawString(soulName_, 4, y); y += 18;
  M5.Display.setTextSize(1);
  snprintf(buf, sizeof buf, "%s soul, %.1f bits", dd::tierName(base.score), base.score);
  M5.Display.drawString(buf, 4, y); y += 10;
  snprintf(buf, sizeof buf, "%s %s %s", dd::traitName(base, dd::C_FEM),
           dd::traitName(base, dd::C_VIBE), dd::traitName(base, dd::C_SKULL));
  M5.Display.drawString(buf, 4, y); y += 10;
  snprintf(buf, sizeof buf, "%s eyes, %s nose", dd::traitName(base, dd::C_EYES), dd::traitName(base, dd::C_NOSE));
  M5.Display.drawString(buf, 4, y); y += 10;
  snprintf(buf, sizeof buf, "hair %s, hat %s", dd::traitName(base, dd::C_HAIR), dd::traitName(base, dd::C_HEADW));
  M5.Display.drawString(buf, 4, y); y += 10;
  snprintf(buf, sizeof buf, "ink %s", dd::traitName(base, dd::C_INK));
  M5.Display.drawString(buf, 4, y); y += 10;
  snprintf(buf, sizeof buf, "id %08x  boot #%u", soulSeed, boots);
  M5.Display.drawString(buf, 4, y);
}

static void dumpScreen() {
  Serial.printf("<<<SHOT %d %d\n", W, H);
  static const char* hx = "0123456789abcdef";
  static char line[135 * 4 + 2];
  for (int y = 0; y < H; y++) {
    int n = 0;
    for (int x = 0; x < W; x++) {
      uint16_t c = fb[y * W + x];
      line[n++] = hx[(c >> 12) & 15];
      line[n++] = hx[(c >> 8) & 15];
      line[n++] = hx[(c >> 4) & 15];
      line[n++] = hx[c & 15];
    }
    line[n++] = '\n';
    Serial.write((const uint8_t*)line, n);
  }
}

static void noteInteraction() {
  lastInteract = millis();
  if (dimmed) { M5.Display.setBrightness(BRIGHT_FULL); dimmed = false; }
}

static void drawLive(int speed, bool sleepy = false) {
  uint32_t t0 = millis();
  live = base;
  dd::applyMood(live, sleepy ? 4 : mood, moodSeed);
  live.turn = dd::clampf(yaw, -0.9f, 0.9f);
  live.pitch = dd::clampf(pitch, -0.4f, 0.45f);
  live.roll = dd::clampf(roll, -0.25f, 0.25f);
  live.gazeX = dd::clampf(gaze, -0.5f, 0.5f);
  if (sleepy || millis() < blinkUntil) live.idx[dd::C_EYES] = 2;
  memcpy(fb, paperBuf, (size_t)W * H * 2);
  dd::drawFace(canvas, cov, live, W * 0.5f, H * 0.46f,
               (float)H * 0.30f * (1.0f + bounce), soulSeed * 31u + (frameNo % 3), speed);
  drawBatteryDots();
  drawMs = millis() - t0;
  M5.Display.startWrite();
  M5.Display.pushImage(0, 0, W, H, fb);
  M5.Display.endWrite();
  if (showCard) cardOverlay();
  if (frozen && !showCard) {  // little pause mark so a freeze is never a mystery
    uint16_t ic = M5.Display.color565(90, 86, 78);
    M5.Display.fillRect(5, 5, 3, 11, ic);
    M5.Display.fillRect(11, 5, 3, 11, ic);
  }
  frameNo++;
  if ((frameNo & 63) == 0) Serial.printf("frame %u ms  bat %d%%%s\n", drawMs, batPct, charging ? "+" : "");
}

static void goToSleep() {
  pitch = 0.3f; gaze = 0;
  drawLive(0, true);
  M5.Display.setTextFont(1);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(M5.Display.color565(120, 116, 108));
  M5.Display.drawString("z z z", W - 40, 24);
  delay(900);
  for (int b = BRIGHT_FULL; b > 0; b -= 12) { M5.Display.setBrightness(b); delay(18); }
  M5.Display.sleep();
  setCpuFrequencyMhz(80);
  asleep = true;
  sleepSince = millis();
  movingSince = 0;
}

static void wakeUp() {
  setCpuFrequencyMhz(240);
  M5.Display.wakeup();
  M5.Display.setBrightness(BRIGHT_FULL);
  dimmed = false;
  asleep = false;
  noteInteraction();
  drawLive(1);
}

static void pet() {
  noteInteraction();
  mood = 1;                       // beams
  moodSeed = soulSeed + frameNo;
  nextMood = millis() + (uint32_t)anim.rr(6000, 10000);
  hopUntil = millis() + 650;
  gazeT = 0;                      // looks right at you
}

static void animate() {
  uint32_t now = millis();

  float ax = 0, ay = 0, az = 0;
  M5.Imu.update();
  bool haveImu = M5.Imu.getAccel(&ax, &ay, &az);
  float imuYaw = 0, imuPitch = 0, upright = 0;
  if (haveImu) {
    float mag = dd::fastSqrt(ax * ax + ay * ay + az * az);
    float dev = fabsf(mag - 1.0f);
    if (dev > 0.8f && now - lastDizzy > 2500) {  // shaken: dizzy, not shuffled
      lastDizzy = now;
      dizzyUntil = now + 1600;
      mood = 3;                                  // wide-eyed
      moodSeed = soulSeed + now;
      nextMood = now + 8000;
      noteInteraction();
    }
    if (dev > 0.22f) noteInteraction();
    upright = dd::clampf((fabsf(ay) - 0.22f) / 0.3f, 0, 1);
    float lean = atan2f(ax, fabsf(ay) + 0.001f);
    float tip = atan2f(az, fabsf(ay) + 0.001f);
    imuYaw = dd::clampf(lean * YAW_GAIN * YAW_SIGN, -0.9f, 0.9f);
    imuPitch = dd::clampf(tip * PITCH_GAIN * PITCH_SIGN, -0.35f, 0.4f);
  }

  if (now > nextGlance) {
    idleYawT = anim.rr(-0.6f, 0.6f);
    idlePitchT = anim.rr(-0.12f, 0.22f);
    idleRollT = anim.rr(-0.08f, 0.08f);
    gazeT = anim.rr(-0.45f, 0.45f);
    nextGlance = now + (uint32_t)anim.rr(2500, 7000);
  }
  if (now > nextMood) {
    int m = anim.ri(0, 6);
    if (m == mood) m = (m + 1 + anim.ri(0, 5)) % 7;
    mood = m;
    moodSeed = soulSeed ^ (now * 2654435761u);
    nextMood = now + (uint32_t)anim.rr(5000, 11000);
  }
  if (now > nextBlink) {
    blinkUntil = now + 140;
    nextBlink = now + (uint32_t)anim.rr(2200, 6000);
  }

  float sway = 0.05f * sinf(now * 0.0021f);
  float yawT = imuYaw * upright + idleYawT * (1 - upright) + sway;
  float pitchT = imuPitch * upright + idlePitchT * (1 - upright) + 0.02f * sinf(now * 0.0013f);
  float rollT = imuYaw * 0.18f * upright + idleRollT * (1 - upright);

  // the hop: a happy little bounce
  bounce = 0;
  if (now < hopUntil) {
    float t = 1.0f - (float)(hopUntil - now) / 650.0f;
    bounce = 0.055f * sinf(t * 3.14159f);
    pitchT -= 0.18f * sinf(t * 3.14159f);
  }
  // dizzy: the room spins
  if (now < dizzyUntil) {
    float t = (float)(dizzyUntil - now) / 1600.0f;  // decays
    rollT += 0.24f * t * sinf(now * 0.02f);
    yawT += 0.3f * t * sinf(now * 0.013f);
  }

  yaw += (yawT - yaw) * 0.42f;
  pitch += (pitchT - pitch) * 0.42f;
  roll += (rollT - roll) * 0.3f;
  gaze += (gazeT + (upright > 0.5f ? imuYaw * 0.4f : 0) - gaze) * 0.3f;

  drawLive(1);
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(0);      // portrait; use 2 if upside down
  M5.Display.setSwapBytes(true);  // native-order RGB565 -> panel byte order
  M5.Display.setBrightness(BRIGHT_FULL);
  W = M5.Display.width();
  H = M5.Display.height();
  fb = (uint16_t*)malloc((size_t)W * H * 2);
  paperBuf = (uint16_t*)malloc((size_t)W * H * 2);
  cov = (uint8_t*)malloc((size_t)W * H);
  if (!fb || !paperBuf || !cov) {
    Serial.println("out of memory");
    for (;;) delay(1000);
  }
  anim = dd::Rng(esp_random());

  loadSoul();
  dd::rollFace(base, soulSeed);
  mood = base.idx[dd::C_EXPR];
  moodSeed = soulSeed;
  yaw = idleYawT = base.turn;
  pitch = idlePitchT = base.pitch;
  roll = idleRollT = base.roll;
  gaze = gazeT = base.gazeX;
  updateBattery();
  lastInteract = millis();

  dd::paperBackground(canvas, soulSeed * 7u + 3u);
  memcpy(paperBuf, fb, (size_t)W * H * 2);
  drawLive(1);
  printCard();

  if (justBorn) {  // a small birth announcement
    M5.Display.setTextFont(1);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(M5.Display.color565(44, 42, 38));
    M5.Display.setTextDatum(top_center);
    M5.Display.drawString(soulName_, W / 2, H - 44);
    M5.Display.setTextSize(1);
    M5.Display.drawString("is born", W / 2, H - 24);
    M5.Display.setTextDatum(top_left);
    hopUntil = millis() + 3300;   // it arrives happy
    mood = 1;
    delay(2600);
  }
}

void loop() {
  uint32_t t0 = millis();
  M5.update();

  if (Serial.available()) {
    char ch = Serial.read();
    if (ch == 's') dumpScreen();
    else if (ch == 'i') {
      for (int k = 0; k < 10; k++) {
        float ax, ay, az;
        M5.Imu.update();
        M5.Imu.getAccel(&ax, &ay, &az);
        Serial.printf("accel % .3f % .3f % .3f\n", ax, ay, az);
        delay(120);
      }
    }
  }

  if (asleep) {
    if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) { wakeUp(); return; }
    float ax, ay, az;
    M5.Imu.update();
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
      uint32_t now = millis();
      bool moved = fabsf(dd::fastSqrt(ax * ax + ay * ay + az * az) - 1.0f) > 0.25f;
      if (now - sleepSince > WAKE_GRACE && moved) {
        if (movingSince == 0) movingSince = now;
        else if (now - movingSince > WAKE_HOLD) { wakeUp(); return; }
      } else {
        movingSince = 0;
      }
    }
    delay(120);
    return;
  }

  if (millis() > nextBatRead) {
    bool wasChg = charging;
    updateBattery();
    nextBatRead = millis() + 4000;
    if ((frozen || showCard) && charging != wasChg) drawLive(0);
  }

  if (M5.BtnA.wasPressed()) {
    pet();
  } else if (M5.BtnB.wasClicked()) {
    noteInteraction();
    showCard = !showCard;
    if (!showCard) frozen = false;  // closing the card always resumes life —
                                    // a slightly-long press registers as a
                                    // hold, and silently freezing felt broken
    drawLive(showCard ? 0 : 1);
  } else if (M5.BtnB.wasHold()) {
    noteInteraction();
    if (!showCard) {
      frozen = !frozen;
      drawLive(frozen ? 0 : 1);
    }
  }

  // on USB power there is nothing to save — stay bright forever
  uint32_t idleFor = charging ? 0 : millis() - lastInteract;
  if (charging && dimmed) { M5.Display.setBrightness(BRIGHT_FULL); dimmed = false; }
  if (!dimmed && idleFor > DIM_AFTER) { M5.Display.setBrightness(BRIGHT_DIM); dimmed = true; }
  if (idleFor > SLEEP_AFTER) { goToSleep(); return; }

  if (!frozen && !showCard) {
    animate();
    uint32_t spent = millis() - t0;
    if (spent < FRAME_MS) delay(FRAME_MS - spent);
  } else {
    delay(20);
  }
}
