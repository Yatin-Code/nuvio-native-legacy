// Implementation of the seek policy. See seek.h for the reasoning behind every
// rule; this file is only the arithmetic.
#include "seek.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// --- PROFILE, READ ONCE FROM THE ENVIRONMENT ---------------------------------
//
// A static, not a const: getenv() is not a constant expression in C, so a
// file-scope const initialised from it would compile and then be a lie. Kept in
// a static and read once in seek_iniciar().
static int profile    = SEEK_CURRENT;
static int logEnabled = 0;
static int started    = 0;

// Today's ladder, which is what CURRENT must reproduce exactly: 10 s (the Apple
// control's own step), then 30 s, 60 s and 120 s, entering at repeats 6, 14 and
// 26. The repeat thresholds are the PLR_SALTO_D1/D2/D3 values from player.c.
static const float STEP_CURRENT[4] = { 10.0f, 30.0f, 60.0f, 120.0f };
static const int   REPEAT_CURRENT[4] = { 0, 6, 14, 26 };

// The Android ladder, from PlayerScrubRates.kt:11-40 — 10 s, 20 s, 30 s, 60 s at
// repeats 0, 3, 8, 15. Note the ceiling is 60 s, lower than the 120 s above,
// and that is deliberate: the Android app has no rung above 60 s, and 120 s per
// repeat overshoots past the point where a person can read the elapsed time and
// land on the frame they meant.
static const float STEP_PARITY[4]  = { 10.0f, 20.0f, 30.0f, 60.0f };
static const int   REPEAT_PARITY[4] = { 0, 3, 8, 15 };

int seek_profile(void) { return profile; }

const char *seek_profile_name(void) {
  switch (profile) {
    case SEEK_PARITY1: return "parity1";
    case SEEK_PARITY2: return "parity2";
    default:           return "current";
  }
}

unsigned seek_repose_ms(void)    { return SEEK_REPOSE_MS; }
unsigned seek_release_ms(void)   { return SEEK_RELEASE_MS; }
unsigned seek_spinner_ms(void)   { return profile == SEEK_CURRENT ? 0u : 800u; }
unsigned seek_tolerance_ms(void) { return profile == SEEK_PARITY2 ? 1500u : 0u; }

float seek_step_seconds(int repeat) {
  const float *steps;
  const int   *at;
  int i, rung = 0;
  if (profile == SEEK_CURRENT) { steps = STEP_CURRENT; at = REPEAT_CURRENT; }
  else                         { steps = STEP_PARITY;  at = REPEAT_PARITY;  }
  // Pick the HIGHEST rung whose starting repeat has already passed. A comparison
  // rather than a sum, because `repeat` jumps when the remote accelerates and
  // summing would step clean over a whole rung without ever sitting in it.
  for (i = 0; i < 4; i++)
    if (repeat >= at[i]) rung = i;
  return steps[rung];
}

void seek_iniciar(void) {
  const char *p, *l;
  if (started) return;
  started = 1;
  profile    = SEEK_CURRENT;   // the default is today's behaviour
  logEnabled = 0;
  p = getenv("NV_SEEK_PROFILE");
  if (p && *p) {
    if      (!strcmp(p, "parity1")) profile = SEEK_PARITY1;
    else if (!strcmp(p, "parity2")) profile = SEEK_PARITY2;
    else if (strcmp(p, "current")) {
      // An unrecognised value must not select a policy. Falling back to CURRENT
      // is what guarantees a typo cannot change the behaviour of someone who
      // never asked for a change.
      fprintf(stderr, "[seek] unknown NV_SEEK_PROFILE \"%s\", using current\n", p);
    }
  }
  l = getenv("NV_DEBUG_SEEK_LOG");
  if (l && *l && strcmp(l, "0")) logEnabled = 1;
  if (profile == SEEK_CURRENT && !logEnabled) return;   // nothing worth saying
  fprintf(stderr,
          "[seek] profile=%s log=%d repose=%ums release=%ums spinner=%ums "
          "tolerance=%ums\n",
          seek_profile_name(), logEnabled, seek_repose_ms(), seek_release_ms(),
          seek_spinner_ms(), seek_tolerance_ms());
  fflush(stderr);
}

// Open a session. The memset is the whole point: seek_reset below keeps the
// counters, so a caller that reached for it here would start the totals from
// whatever happened to be in memory.
void seek_begin(SeekPolicy *p) {
  if (p) memset(p, 0, sizeof *p);
}

void seek_reset(SeekPolicy *p) {
  if (!p) return;
  // `position` is left alone on purpose: the player owns where the bar starts
  // and a reset must not throw it back to zero mid-session.
  p->active       = 0;
  p->repeat       = 0;
  p->lastGesture  = 0;
  p->reposeAt     = 0;
  p->needSeek     = 0;
  p->spinnerUntil = 0;
  // The counters are NOT cleared: they are session totals, and the log line
  // prints them, so wiping them per session would hide the very number Phase 0
  // is looking for.
}

void seek_gesture(SeekPolicy *p, int dir, float pos, float dur, unsigned now) {
  if (!p) return;
  if (!p->active) {
    p->active  = 1;
    p->repeat  = 0;
  }
  p->repeat++;
  p->gestures++;
  p->position = pos + (float)dir * seek_step_seconds(p->repeat);
  if (dur > 0.0f) {
    if (p->position < 0.0f)  p->position = 0.0f;
    if (p->position > dur)   p->position = dur;
  }
  p->lastGesture = now;
  // The settle delay runs from the LAST gesture, not the first. That is what
  // turns four presses inside one hold into a single seek instead of four seeks
  // spaced 350 ms apart, which is the measured defect this delay exists to fix.
  p->reposeAt = now + seek_repose_ms();
  p->needSeek = 1;
  // The person is moving the position right now, so a spinner left over from a
  // previous seek no longer describes anything. Without this, letting go of the
  // key and waiting out the settle would flash the loading icon for a scrub that
  // was already cancelled.
  p->spinnerUntil = now + seek_spinner_ms();
  if (logEnabled) seek_log(p, "gesture", now);
}

int seek_release(SeekPolicy *p, unsigned now) {
  int wasActive;
  if (!p || !p->active) return 0;
  wasActive    = p->active;
  p->active    = 0;
  p->repeat    = 0;
  p->reposeAt  = 0;      // seek_tick re-arms on the next key press
  p->needSeek  = 0;
  if (logEnabled) seek_log(p, "released", now);
  return wasActive;
}

int seek_tick(SeekPolicy *p, unsigned now) {
  if (!p || !p->needSeek) return 0;
  if (p->reposeAt && now < p->reposeAt) return 0;   // still settling
  p->needSeek = 0;
  p->reposeAt = 0;
  p->commands++;
  if (logEnabled) seek_log(p, "sent", now);
  return 1;
}

int seek_spinner_visible(const SeekPolicy *p, int buffering, unsigned now) {
  if (!p) return buffering ? 1 : 0;
  if (!buffering) return 0;
  // The window is measured from the LAST gesture. Until it expires, the
  // bufferingStart that lands right after a seek does not become an icon — which
  // is what the Android app does with SEEK_SUPPRESS_TIMEOUT_MS, and what removes
  // the loading flicker from a seek.
  if (p->spinnerUntil && now < p->spinnerUntil) return 0;
  return 1;
}

int seek_within_tolerance(double pos, double target) {
  unsigned tol = seek_tolerance_ms();
  if (!tol) return 0;
  if (target <= 0.0) return 0;
  return fabs(pos - target) * 1000.0 <= (double)tol;
}

void seek_log(const SeekPolicy *p, const char *event, unsigned now) {
  if (!p || !logEnabled) return;
  fprintf(stderr,
          "[seek] %-8s t=%-6u pos=%-8.1f repeat=%-3d gestures=%-3u commands=%-3u\n",
          event ? event : "?", now, (double)p->position, p->repeat,
          p->gestures, p->commands);
  fflush(stderr);
}
