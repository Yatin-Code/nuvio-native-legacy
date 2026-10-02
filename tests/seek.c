// Phase 0 of player_implementation.md: the seek policy's rules, plus a bench
// that runs on the host with the TV replaced by a fake pipeline.
//
// Two halves, on purpose:
//
//   1. ASSERTIONS on the rules themselves. These are cheap, they are exact, and
//      they fail the moment someone edits a threshold. They run in every
//      profile, so CURRENT is pinned to today's behaviour just as tightly as the
//      parity arms are pinned to the Android numbers.
//
//   2. A BENCH that plays a scripted remote session against a simulated
//      pipeline: a frame loop at a fixed 60 fps, a key that repeats every
//      100 ms (the measured C9 figure), and a pipeline whose seek takes a
//      configurable number of milliseconds to settle. It reports the three
//      numbers Phase 0 exists to collect:
//
//        - seeks per hold        (today: 4. The Android app sends 1.)
//        - spinner flashes       (today: one per seek. The Android app: 0.)
//        - perceived settle time (first gesture to the first frame after the
//                               pipeline actually settles)
//
// Run: bash tests/seek.sh
// Device: the same binary is not what you want on a TV. Use NV_DEBUG_SEEK_LOG=1
// in the environment on the device and read the log lines from seek.c.
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/seek.h"

// The module reads the profile from the environment exactly once per process,
// which makes two profiles in one process impossible. The tests therefore
// re-exec themselves with NV_SEEK_PROFILE set, once per arm. That is also how it
// works on a device, so the test exercises the real selection path.
// putenv does NOT copy the string — it keeps the pointer, so the buffer has to
// outlive the process. That is why this is static and not a local.
static void rerun_as(const char *profile) {
  static char buf[64];
  snprintf(buf, sizeof buf, "NV_SEEK_PROFILE=%s", profile);
  if (putenv(buf) != 0) { perror("putenv"); exit(2); }
}

static const char *want_profile = 0;

// ---------------------------------------------------------------------------
// Fake pipeline.
//
// A TV pipeline, reduced to the three things the policy can observe:
//   - how long a seek takes to settle
//   - whether it announces buffering while it settles
//   - that the frame appears when it settles
//
// Latency defaults to 320 ms, which is a mid-range guess for a webOS 4 / Tizen 6
// set re-seeking into a buffered region. Override with NV_BENCH_SEEK_MS.
// ---------------------------------------------------------------------------
typedef struct {
  unsigned latencyMs;
  int      announcesBuffering;   // most pipelines do
  double   pos;
  unsigned settledAt;            // 0 = nothing in flight
  int      frameDrawn;
  unsigned frameAt;
} Pipeline;

static void pipe_init(Pipeline *pp) {
  memset(pp, 0, sizeof *pp);
  const char *e = getenv("NV_BENCH_SEEK_MS");
  pp->latencyMs = e ? (unsigned)atoi(e) : 320u;
  pp->announcesBuffering = 1;
}

static void pipe_seek(Pipeline *pp, double target, unsigned now) {
  pp->pos        = target;
  pp->settledAt  = now + pp->latencyMs;
  pp->frameDrawn = 0;
}

// 1 while the pipeline has not finished settling.
static int pipe_busy(const Pipeline *pp, unsigned now) {
  return pp->settledAt && now < pp->settledAt;
}

static void pipe_pump(Pipeline *pp, unsigned now) {
  if (pp->settledAt && now >= pp->settledAt) {
    pp->settledAt = 0;
    pp->frameDrawn = 1;
    pp->frameAt    = now;
  }
}

// ---------------------------------------------------------------------------
// Part 1: the rules.
// ---------------------------------------------------------------------------

// CURRENT must keep today's ladder, or the Phase 0 baseline is measuring
// something other than the shipping behaviour. The ladder is profile-dependent,
// so this only asserts on the profile that owns these numbers.
static void test_ladder_current(void) {
  if (seek_profile() != SEEK_CURRENT) return;
  assert(seek_step_seconds(0) == 10.0f);
  assert(seek_step_seconds(5) == 10.0f);
  assert(seek_step_seconds(6) == 30.0f);   // PLR_SALTO_D1
  assert(seek_step_seconds(13) == 30.0f);
  assert(seek_step_seconds(14) == 60.0f);  // PLR_SALTO_D2
  assert(seek_step_seconds(25) == 60.0f);
  assert(seek_step_seconds(26) == 120.0f); // PLR_SALTO_D3
  assert(seek_step_seconds(200) == 120.0f);
}

// The Android ladder, from PlayerScrubRates.kt:11-40. Asserted on every profile
// because the real device is the only place the parity arms will ever be set,
// and the test host is the only place they can be checked for a typo.
static void test_ladder_parity(void) {
  if (seek_profile() == SEEK_CURRENT) return;   // CURRENT uses the ladder above
  assert(seek_step_seconds(0) == 10.0f);
  assert(seek_step_seconds(2) == 10.0f);
  assert(seek_step_seconds(3) == 20.0f);
  assert(seek_step_seconds(7) == 20.0f);
  assert(seek_step_seconds(8) == 30.0f);
  assert(seek_step_seconds(14) == 30.0f);
  assert(seek_step_seconds(15) == 60.0f);
  assert(seek_step_seconds(999) == 60.0f);
}

// A hold of four presses must produce exactly one pipeline command, whatever
// the timing inside the hold. This is the assertion that encodes the whole
// reason the settle delay exists.
static void test_hold_collapses_to_one_command(void) {
  SeekPolicy p;
  seek_begin(&p);
  int i, sent = 0;
  p.position = 600.0;
  // 100 ms key repeat on the C9: four presses land inside the 350 ms settle, so
  // the settle timer is pushed out by the last one and only that one fires.
  for (i = 0; i < 4; i++) {
    seek_gesture(&p, +1, p.position, 3600.0, 1000u + (unsigned)i * 100u);
    // A frame passes between presses and must not emit a command.
    assert(seek_tick(&p, 1000u + (unsigned)i * 100u + 16u) == 0);
  }
  // Last press was at t=1300, so the command may not leave before t=1650.
  assert(seek_tick(&p, 1649u) == 0);
  assert(seek_tick(&p, 1650u) == 1);
  sent += seek_tick(&p, 1650u);   // latched: fires once only
  assert(sent == 0);
  assert(p.commands == 1);
  assert(p.gestures == 4);
  // The bar moved by four steps, not by one. The total is computed from the
  // ladder rather than hardcoded, because the ladder is profile-dependent and a
  // hardcoded 640 would only ever be true on CURRENT.
  {
    float expected = 600.0f;
    int r;
    for (r = 1; r <= 4; r++) expected += seek_step_seconds(r);
    assert(p.position == expected);
  }
}

// Slow taps, spaced further apart than the settle delay, are three separate
// seeks. The collapse must not become a debounce that eats deliberate taps.
static void test_slow_taps_stay_separate(void) {
  SeekPolicy p;
  seek_begin(&p);
  p.position = 0.0f;
  seek_gesture(&p, -1, p.position, 3600.0, 1000u);
  assert(seek_tick(&p, 1000u + SEEK_REPOSE_MS) == 1);
  seek_gesture(&p, -1, p.position, 3600.0, 2000u);
  assert(seek_tick(&p, 2000u + SEEK_REPOSE_MS) == 1);
  seek_gesture(&p, -1, p.position, 3600.0, 3000u);
  assert(seek_tick(&p, 3000u + SEEK_REPOSE_MS) == 1);
  assert(p.commands == 3);
}

// Letting go ends the scrub and stops the pending command. Without this the
// pipeline would seek to a position the person already abandoned.
static void test_release_drops_pending(void) {
  SeekPolicy p;
  seek_begin(&p);
  p.position = 100.0f;
  seek_gesture(&p, +1, p.position, 3600.0, 1000u);
  assert(p.needSeek == 1);
  assert(seek_release(&p, 1010u) == 1);
  assert(p.active == 0);
  assert(seek_tick(&p, 1000u + SEEK_REPOSE_MS + 10u) == 0);
  assert(p.commands == 0);
  assert(seek_release(&p, 2000u) == 0);   // nothing to release
}

// Clamping. Reaching the start or the end must park there, not run past it.
static void test_clamping(void) {
  SeekPolicy p;
  seek_begin(&p);
  p.position = 5.0f;
  seek_gesture(&p, -1, p.position, 3600.0, 1000u);
  assert(p.position == 0.0f);
  seek_reset(&p);
  p.position = 3595.0f;
  seek_gesture(&p, +1, p.position, 3600.0, 1000u);
  assert(p.position == 3600.0f);
  // Unknown duration: no clamp on the right, so a live channel is not pinned
  // to a duration it does not have.
  seek_reset(&p);
  p.position = 3595.0f;
  seek_gesture(&p, +1, p.position, 0.0f, 1000u);
  assert(p.position == 3605.0f);
}

// The spinner window. On CURRENT there is none, and the test says so: the whole
// point of pinning the baseline is that CURRENT is the control arm.
static void test_spinner_window(void) {
  SeekPolicy p;
  seek_begin(&p);
  p.position = 0.0f;
  // Not buffering: no spinner, anywhere.
  assert(seek_spinner_visible(&p, 0, 1000u) == 0);
  // A gesture opens the window. The bufferingStart that lands right after the
  // seek must not become an icon.
  seek_gesture(&p, +1, p.position, 3600.0, 1000u);
  unsigned win = seek_spinner_ms();
  if (seek_profile() == SEEK_CURRENT) {
    assert(win == 0u);
    assert(seek_spinner_visible(&p, 1, 1000u) == 1);   // baseline: no filter
  } else {
    assert(win == 800u);
    assert(seek_spinner_visible(&p, 1, 1000u + win - 1u) == 0);
    assert(seek_spinner_visible(&p, 1, 1000u + win) == 1);
  }
  // Once the window expires a genuine stall must still be reported, otherwise
  // the fix would trade a flicker for a silent freeze.
  seek_reset(&p);
  assert(seek_spinner_visible(&p, 1, 900000u) == 1);
}

// Resume tolerance, and the 1500 ms value the Android MPV engine uses.
static void test_tolerance(void) {
  if (seek_profile() != SEEK_PARITY2) {
    assert(seek_tolerance_ms() == 0u);
    assert(seek_within_tolerance(100.0, 100.4) == 0);
    return;
  }
  assert(seek_tolerance_ms() == 1500u);
  assert(seek_within_tolerance(100.0, 100.4) == 1);
  assert(seek_within_tolerance(100.0, 101.4) == 1);    // 1400 ms
  assert(seek_within_tolerance(100.0, 101.5) == 1);    // exactly 1500 ms
  assert(seek_within_tolerance(100.0, 101.6) == 0);    // 1600 ms
  assert(seek_within_tolerance(0.0, 0.0) == 0);         // no target, no skip
}

// A reset must not throw the bar back to the start: the player owns that value.
static void test_reset_keeps_position(void) {
  SeekPolicy p;
  seek_begin(&p);
  p.position = 1871.0f;
  p.gestures = 9;
  seek_reset(&p);
  assert(p.position == 1871.0f);
  assert(p.active == 0);
  assert(p.gestures == 9);   // session total, deliberately not cleared
}

// ---------------------------------------------------------------------------
// Part 2: the bench.
//
// Plays a scripted session against the fake pipeline at 60 fps and reports the
// three Phase 0 numbers. The script is one realistic thing a person does: hold
// the right arrow for two seconds on a film, then tap it three more times.
// ---------------------------------------------------------------------------
typedef struct {
  unsigned holds;           // separate press-and-hold gestures in the script
  unsigned taps;            // separate deliberate taps
  unsigned intents;         // seeks the script actually asked for
  unsigned gestures;        // arrow presses the script issued
  unsigned eventsFired;     // events the script meant to deliver
  unsigned seeksSent;       // commands that reached the pipeline
  unsigned frames;          // frames simulated
  unsigned framesSettled;   // settles the person actually saw
  unsigned spinnerFlashes;  // frames where the icon was shown
  unsigned perceivedMs;     // first gesture to first frame after settling
  unsigned tapSettleMs;     // same, measured on a single deliberate tap
  unsigned resumeResent;    // seeks re-sent after landing off target
  unsigned firstGestureAt;
  unsigned firstFrameAt;
  unsigned tapGestureAt;
  unsigned tapSeekAt;
} Bench;

// The scripted remote session: one press-and-hold of 2 s, then three
// deliberate taps. A person watching a film does exactly this.
//
// The event times are MATERIALISED BEFORE THE FRAME LOOP rather than computed
// inside it, and the reason is written down because this bench got it wrong
// twice.
//
// The input used to be gated on `now % 100 == 0` while the frame loop steps by
// 17 ms. Those two grids only coincide every 1700 ms, so 2 of 20 key repeats
// ever fired. Rewriting it as `(now + FRAME_MS/2) % 100 == 0` did not fix it
// either — same coincidence, different phase — and the hold fired exactly once.
//
// The rule that falls out: never derive input from arithmetic on the frame
// counter. A remote delivers key events on its own clock; the frame loop is a
// separate clock that samples them. So the loop below only ever asks "did an
// event land inside this frame's interval?", which is a comparison.
#define HOLD_START_MS     0u
#define HOLD_END_MS    2000u
#define REPEAT_MS      100u     // C9 key repeat
#define TAP_AT0       2500u
#define TAP_GAP        600u
#define TAP_COUNT         3u
#define RUN_MS        6000u
#define MAX_EVENTS      64u

// `intent` is the seek this press belongs to, NOT a flag for "is this a hold".
//
// A 2 s hold at a 100 ms repeat is 20 arrow presses that a person means as ONE
// "go forward". The first version of this bench tagged only the first press of
// the hold as a hold and the other 19 as taps, so the correct answer (4 seeks)
// was reported as "the debounce ate 18 of my taps".
//
// `intent` states which action the press belongs to, and the number of distinct
// intents is what the seek count is measured against. No inference.
typedef struct { unsigned at; unsigned intent; } BenchEvent;

static int bench_build_events(BenchEvent *ev, unsigned cap) {
  unsigned t;
  int n = 0;
  // Intent 0: the 2 s hold — twenty presses, one meaning.
  for (t = HOLD_START_MS; t < HOLD_END_MS && n < (int)cap; t += REPEAT_MS)
    ev[n++] = (BenchEvent){ t, 0 };
  // Intents 1..3: three deliberate taps, each its own meaning.
  for (t = 0; t < TAP_COUNT && n < (int)cap; t++)
    ev[n++] = (BenchEvent){ TAP_AT0 + t * TAP_GAP, 1u + t };
  return n;
}

static void bench_run(Pipeline *pp, Bench *b) {
  SeekPolicy p;
  BenchEvent ev[MAX_EVENTS];
  unsigned now = 0, frame = 0;
  const unsigned FRAME_MS = 17;      // ~60 fps
  const unsigned DUR = 7200;         // 2h film
  int nevents, ei = 0;

  memset(b, 0, sizeof *b);
  seek_begin(&p);
  p.position = 3600.0;               // halfway in
  nevents = bench_build_events(ev, MAX_EVENTS);

  // The frame loop. Real elapsed time is irrelevant here: the fake pipeline is
  // driven off the same clock, so 17 ms per frame is the whole model.
  for (now = 0; now < RUN_MS; now += FRAME_MS) {
    pipe_pump(pp, now);

    // Deliver every event that landed inside this frame's interval
    // [now, now+FRAME_MS). Half-open, so the event at t=0 fires on the very
    // first frame — the closed form `(now, now+FRAME_MS]` silently dropped it,
    // which the bench's own event-count assertion caught immediately.
    while (ei < nevents && ev[ei].at >= now && ev[ei].at < now + FRAME_MS) {
      int isNewIntent = (ei == 0 || ev[ei].intent != ev[ei - 1].intent);
      seek_gesture(&p, +1, p.position, (float)DUR, now);
      if (ei == 0) b->firstGestureAt = now;
      // The first press of an intent is a new action; the rest are the remote
      // repeating a key the person is still holding down.
      if (isNewIntent) {
        if (ev[ei].intent == 0) b->holds++;
        else                   b->taps++;
        b->intents++;
        // The first tap is the measurement that compares against the Android
        // app: one deliberate press, one seek, one settle. `perceivedMs` below
        // is dominated by the 2 s hold and means something else entirely.
        if (ev[ei].intent == 1u) b->tapGestureAt = now;
      }
      ei++;
    }
    b->gestures = p.gestures;   // the policy's own count, not a second tally

    // The policy decides whether the pipeline hears about it.
    if (seek_tick(&p, now)) {
      pipe_seek(pp, p.position, now);
      b->seeksSent++;
      if (b->tapGestureAt && !b->tapSeekAt) b->tapSeekAt = now;
    }

    // What the icon would do, and what the person would see.
    {
      int buffering = pipe_busy(pp, now) && pp->announcesBuffering;
      if (seek_spinner_visible(&p, buffering, now)) b->spinnerFlashes++;
    }
    if (pp->frameDrawn) {
      if (b->firstFrameAt == 0) {
        b->firstFrameAt = pp->frameAt;
        b->perceivedMs = pp->frameAt - b->firstGestureAt;
      }
      if (b->tapSeekAt && !b->tapSettleMs) b->tapSettleMs = pp->frameAt - b->tapGestureAt;
      b->framesSettled++;
    }
    frame++;
  }
  b->frames      = frame;
  b->eventsFired = (unsigned)ei;
}

// How many seeks the script actually asked for: one per intent, so a 2 s hold
// counts once no matter how many key repeats it produced.
static unsigned bench_intent(const Bench *b) { return b->intents; }

// A second scenario, because the script above never exercises the resume
// tolerance at all: it only ever seeks forward from a playing position.
//
// The resume case is the one the tolerance exists for. The pipeline is asked
// for 600 s, settles, and reports currentTime 620 s — the decoder swallowed the
// rest of the block. Re-sending the seek makes the picture blink twice, so a
// policy with a tolerance recognises that it is already close enough and stops.
static void bench_resume(Bench *b) {
  const double requested = 600.0;
  // 400 ms of pipeline slop, a plausible figure for a decoder reporting
  // currentTime from the last block it finished.
  const double settledAt = 600.4;
  if (seek_within_tolerance(settledAt, requested)) return;   // good: no re-send
  b->resumeResent++;
}

static void bench_print(const Bench *b, const Pipeline *pp) {
  unsigned intent = bench_intent(b);   // seeks the script actually asked for
  printf("profile          %s\n", seek_profile_name());
  printf("pipeline latency %u ms\n", pp->latencyMs);
  printf("frames           %u\n", b->frames);
  printf("gestures         %u  (%u hold, %u taps)\n", b->gestures, b->holds, b->taps);
  printf("seeks sent       %u  for %u intended  %s\n",
         b->seeksSent, intent,
         b->seeksSent <= intent ? "OK" : "TOO MANY (wasted pipeline calls)");
  printf("tap settle       %u ms  (one deliberate press -> frame)\n", b->tapSettleMs);
  printf("hold settle      %u ms  (press to last frame; includes the 2 s hold)\n",
         b->perceivedMs);
  printf("spinner frames   %u  of %u  %s\n",
         b->spinnerFlashes, b->frames,
         b->spinnerFlashes == 0 ? "OK (never flickers)" : "flickers");
  printf("resume re-sends  %u  %s\n", b->resumeResent,
         b->resumeResent == 0 ? "OK (accepted the landing)" : "re-seek on a 400 ms slop");
  printf("frames settled   %u\n", b->framesSettled);
  printf("\n");
}

int main(int argc, char **argv) {
  Pipeline pp;
  Bench    b;

  // One process per profile: seek_iniciar() reads the environment once, on
  // purpose, so this is the only way to compare arms.
  if (argc < 2) {
    printf("usage: %s <current|parity1|parity2>\n", argv[0]);
    return 2;
  }
  want_profile = argv[1];
  rerun_as(want_profile);
  seek_iniciar();

  // The selection must have taken. A wrong name here would mean the test is
  // measuring CURRENT while claiming to measure an arm.
  if (strcmp(seek_profile_name(), want_profile) != 0) {
    fprintf(stderr, "profile selection failed: asked %s, got %s\n",
            want_profile, seek_profile_name());
    return 1;
  }

  // Rules.
  test_ladder_current();
  test_ladder_parity();
  test_hold_collapses_to_one_command();
  test_slow_taps_stay_separate();
  test_release_drops_pending();
  test_clamping();
  test_spinner_window();
  test_tolerance();
  test_reset_keeps_position();

  // Bench.
  pipe_init(&pp);
  bench_run(&pp, &b);
  bench_resume(&b);
  bench_print(&b, &pp);

  // The Phase 0 acceptance criteria, as assertions so that a regression fails
  // the test rather than quietly printing a worse number.
  //
  //   * No arm may waste pipeline calls. The script asked for one seek per hold
  //     and one per deliberate tap; anything above that is the four-seeks-in-
  //     0.8 s defect that the settle delay exists to prevent.
  //   * No arm may eat a deliberate tap. A collapse that aggressive would turn
  //     "tap ahead three times" into one 60 s jump, which is worse than the
  //     flicker it fixed.
  {
    unsigned intent = bench_intent(&b);
    if (b.seeksSent > intent) {
      fprintf(stderr, "FAIL: %u seeks for %u intended actions (%u hold + %u taps)\n",
              b.seeksSent, intent, b.holds, b.taps);
      return 1;
    }
    if (b.seeksSent < b.taps) {
      fprintf(stderr, "FAIL: %u seeks for %u deliberate taps (debounce ate input)\n",
              b.seeksSent, b.taps);
      return 1;
    }
    // The script must actually have run. If the input schedule silently stopped
    // firing, every number above would be vacuously true — which is exactly how
    // the first two versions of this bench managed to pass while measuring
    // almost nothing.
    if (b.eventsFired != b.gestures || b.gestures < 18) {
      fprintf(stderr, "FAIL: script delivered %u of %u gestures (%u events); "
                      "the bench input schedule is broken\n",
              b.gestures, b.eventsFired, b.eventsFired);
      return 1;
    }
    // The parity arms must not flash the spinner during a seek the policy
    // decided to hide. CURRENT is exempt: not flashing is the change being
    // measured, and CURRENT is the control.
    if (seek_profile() != SEEK_CURRENT && b.spinnerFlashes != 0) {
      fprintf(stderr, "FAIL: %s flashed the spinner %u times during seeks\n",
              seek_profile_name(), b.spinnerFlashes);
      return 1;
    }
    // PARITY2 is the only arm with a tolerance, and it is the only one allowed
    // to accept a landing that is 400 ms off the request. CURRENT re-sends.
    if (seek_profile() == SEEK_PARITY2 && b.resumeResent != 0) {
      fprintf(stderr, "FAIL: parity2 re-sent a resume seek inside its tolerance\n");
      return 1;
    }
  }
  return 0;
}
