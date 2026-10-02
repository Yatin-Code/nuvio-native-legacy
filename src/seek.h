// SEEK POLICY — the single place that decides how a seek behaves.
//
// This is Phase 0 of player_implementation.md. Before this file the same
// behaviour was scattered across the tree: the step ladder and the end-of-scrub
// timer lived in player.c (PLR_SALTO_*, PLR_SCRUB_FIM_MS), while the 350 ms
// settle delay was written out three times — once per video backend, in
// video.c, video_tizen.c and video_tpk.c, each with its own #define and its own
// copy of the seekAlvo/seekEm pair.
//
// You cannot measure behaviour that lives in three places. The two numbers that
// actually decide whether the native app feels like the Android app are:
//
//   1. how many seeks reach the TV per press-and-hold of the arrow key, and
//   2. whether a loading spinner flashes during a seek that was going to be fine.
//
// Both need one account, and the bench needs to run that account on the host
// with no TV attached. That is all this module is.
//
// WHAT THIS MODULE OWNS, AND WHAT IT DOES NOT
//
// Owns: the step size per key repeat, the settle delay before the pipeline is
//       told anything, "one command per scrub", the window in which the
//       buffering spinner must stay hidden, and the resume tolerance.
//
// Does NOT own: pausing and resuming playback during a scrub, drawing the
//       progress bar, the pipeline itself, track selection, HDR. player.c
//       already keeps its own scrubTocava for the pause bookkeeping and this
//       module deliberately does not duplicate it — two owners of "is it
//       playing" is exactly the class of bug Phase 0 is here to prevent.
//
// Consequence: this module links with no SDL, no network and no device, which
// is why the bench and the unit test below are plain host C.
#ifndef NV_SEEK_H
#define NV_SEEK_H

// --- THE SETTLE DELAY ---------------------------------------------------------
//
// 350 ms, and this is a MEASURED value, not a taste knob. The original comment
// in video.c recorded the measurement: holding the arrow key produced FOUR seeks
// inside 0.8 s (16 s, 26 s, 36 s, 46 s) — four consecutive range requests
// against the same source, and roughly 71 s later the pipeline died. It is not
// proven that one caused the other, but the first three are discarded the
// instant the fourth lands, so sending four positions when the person asked for
// one is waste under any theory.
//
// This number now lives here because all three backends need it and must not
// disagree.
//
// It is spelled SEEK_REPOSE_MS, NOT SEEK_REPOUSO_MS, and that difference is
// deliberate: video.c and video_tizen.c each still carry their own
// `#define SEEK_REPOUSO_MS 350` from before this file existed. Giving the two
// names different spellings means that when Phase 2 deletes the local copies and
// routes the backends through seek_repose_ms(), the compiler cannot let a stale
// one slip through unnoticed — which a same-spelled macro would happily do,
// since 350 and 350u are different tokens and would warn at best.
#define SEEK_REPOSE_MS 350u

// A scrub ends after this much silence. Longer than the remote's key-repeat
// interval (measured near 100 ms on the C9) and shorter than a human's reaction
// time after deliberately letting go. Same value as PLR_SCRUB_FIM_MS.
#define SEEK_RELEASE_MS 420u

// --- PROFILES ----------------------------------------------------------------
//
// These are EXPERIMENT ARMS, not user preferences. Each is a different seek
// policy, and the device decides which one is worth keeping. The default is
// today's behaviour, byte for byte, so the bench can record a baseline without
// having changed anything.
//
//   CURRENT   what 1.6.x does: 10/30/60/120 s steps, no spinner window. This is
//             the baseline the other two get compared against.
//   PARITY1   the Android step ladder plus an 800 ms spinner window. The steps
//             come from the Android app's PlayerScrubRates.kt (10/20/30/60 s at
//             repeats 0/3/8/15); the 800 ms is its SEEK_SUPPRESS_TIMEOUT_MS.
//             Costs the pipeline nothing — it only hides the indicator for the
//             first 800 ms.
//   PARITY2   PARITY1 plus a 1500 ms resume tolerance, the same value the
//             Android app uses for its MPV engine (PlayerRuntimeControllerMpv).
//             A resume seek that already landed inside the tolerance is not
//             re-sent, which is what stops the picture blinking twice.
//
// Read from the environment once, in seek_iniciar():
//   NV_SEEK_PROFILE=current|parity1|parity2
//   NV_DEBUG_SEEK_LOG=1      one line per scrub event, on stderr
//
// Absent, or set to something unrecognised, CURRENT wins. An unrecognised value
// must never pick a different policy: the point of the default is that a typo
// cannot change the behaviour of someone who never asked for a change.
enum { SEEK_CURRENT = 0, SEEK_PARITY1 = 1, SEEK_PARITY2 = 2 };

// --- SCRUB STATE -------------------------------------------------------------
//
// While `active` is 1, `position` belongs to the scrub and NOT to the pipeline.
//
// This ownership split is the fix for the bug where the progress bar snapped
// back on its own. player_atualizar rewrites the displayed position from
// video_pos() every frame, but the command to the pipeline is deliberately held
// back for 350 ms. In the gap between the two, the real position was still the
// old one and overwrote the position the person had just chosen. While a scrub
// is running, the scrub owns the value.
typedef struct {
  int      active;        // 1 = scrub in progress, `position` is ours
  int      repeat;        // key repeats so far in this scrub
  float    position;      // the CHOSEN position, in seconds
  unsigned lastGesture;   // timestamp of the most recent gesture
  unsigned reposeAt;      // when the pipeline may finally be told (0 = none)
  int      needSeek;      // read-and-cleared by seek_tick: 1 = send it now
  unsigned spinnerUntil;  // spinner stays hidden until here (0 = no window)

  // Phase 0 counters. Harmless in production, and they are the whole point: the
  // log line and the bench both reduce to these.
  unsigned gestures;      // arrow presses received
  unsigned commands;      // seeks that actually reached the pipeline
} SeekPolicy;

// Once, at app start. Reads NV_SEEK_PROFILE and NV_DEBUG_SEEK_LOG. Idempotent:
// a second call is ignored, because the host bench must not end up with two
// different profiles inside one process.
void seek_iniciar(void);

// --- PROFILE QUERIES ---------------------------------------------------------
int         seek_profile(void);
const char *seek_profile_name(void);
unsigned    seek_repose_ms(void);      // 350
unsigned    seek_release_ms(void);     // 420
unsigned    seek_spinner_ms(void);     // 0 on CURRENT, 800 on the parity arms
unsigned    seek_tolerance_ms(void);   // 0 except on PARITY2, where it is 1500
// Step size for the next press, in seconds. `repeat` is the repeat count (0 is
// the first tap). A pure function of `repeat` — no state — so the test can
// exercise the whole ladder without a scrub.
float       seek_step_seconds(int repeat);

// --- LIFECYCLE ---------------------------------------------------------------
// Two different operations, and conflating them was the first bug this module
// had: a caller reached for `reset` to open a session, `reset` deliberately
// leaves the counters alone, and the totals then started from whatever was on
// the stack.

// Open a playback session. Zeroes EVERYTHING, counters included, so a caller
// never has to know whether the struct was on the stack or in a static. Call
// once per source the player opens.
void seek_begin(SeekPolicy *p);

// Re-arm the scrub without touching the session: clears the ladder, the timers
// and the spinner window, but keeps `position` (the player owns where the bar
// is) and the counters (they are session totals, and seek_log prints them).
//
// Call on a stop, on a source change, and after any seek that was not a
// gesture.
void seek_reset(SeekPolicy *p);

// --- THE GESTURE -------------------------------------------------------------
// One arrow press. `dir` < 0 rewinds, > 0 advances. `pos` is where the bar is
// now, `dur` the known duration in seconds (0 or less = unknown, so no clamp on
// the right).
//
// The pipeline command is NEVER sent here — it is armed for the settle delay.
// That delay is what folds four presses of one held key into a single seek.
void seek_gesture(SeekPolicy *p, int dir, float pos, float dur, unsigned now);

// The person let go of the key. Without this the pipeline would sit waiting out
// the settle delay even though the bar had stopped moving. Returns 1 if a scrub
// was actually running.
int  seek_release(SeekPolicy *p, unsigned now);

// --- PER FRAME ---------------------------------------------------------------
// What video_bombear calls. Returns 1 when the settle delay has elapsed and
// there is a position to send; the caller then calls video_buscar().
//
// The pause/resume of the surrounding scrub stays in player.c.
int  seek_tick(SeekPolicy *p, unsigned now);

// --- THE SPINNER -------------------------------------------------------------
// 1 when the buffering indicator SHOULD be shown.
//
// This is the item that makes a seek feel equivalent to the Android app without
// touching the decoder. The pipeline needs a few hundred milliseconds to settle
// after a seek and a bufferingStart event arrives right behind it; painting the
// icon for 200 ms is exactly what makes a seek read as slow. On CURRENT the
// spinner just follows the pipeline, which is what the TV does today. On the
// parity arms the event is swallowed until seek_spinner_ms() has passed.
//
// `buffering` is what the pipeline reports right now (on LG,
// video_bufferando_ms() > 0; the Tizen AVPlay gives no such signal at all, and
// the caller passes 0).
int  seek_spinner_visible(const SeekPolicy *p, int buffering, unsigned now);

// --- RESUME TOLERANCE --------------------------------------------------------
// 1 when `pos` is already close enough to `target` that the seek can be treated
// as a no-op. 0 means send it anyway.
//
// The tolerance exists for the same reason the Android app has one: the
// pipeline sometimes settles at a currentTime a few hundred ms off the request
// (the decoder swallows the rest of the block and only currentTime gets
// reported), and re-sending the seek because of that makes the picture blink
// twice. 1500 ms is the value the Android app uses for its MPV engine.
int  seek_within_tolerance(double pos, double target);

// --- LOG ---------------------------------------------------------------------
// One line per scrub event on stderr, and only with NV_DEBUG_SEEK_LOG=1. This is
// Phase 0 taking its baseline on a real device: seeks per hold, and the delay
// from first gesture to first spinner, are the two measurements Phase 2 needs.
void seek_log(const SeekPolicy *p, const char *event, unsigned now);

#endif
