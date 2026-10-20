// EVIDENCE-Fixture Goal-C (unblocks the real-screen rule for screen lanes).
//
// Headless --preview has no account/data, so every capture shows the
// cover/onboarding screen. This module feeds fake-but-realistic data into the
// headless app (catalog rows, saved list, stream entries, related titles,
// simulated playback) and executes scripted screen jumps, so the parity shot
// scripts can capture REAL screens (home rows, detail, stream sheet, player
// OSD, post-play card, search, library).
//
// SHOT-GATED like the P0.5 probe (anim.h): every line of fixture.c compiles
// ONLY under -DNV_FIXTURE, which no release script defines, and even then it
// stays inert unless the runtime env NUVIO_FIXTURE=1 is set. Without the flag
// the release path is untouched (prove with: nm | grep fixture -> empty).
// NV_EVIDENCIA_MOLA is NOT touched and stays shot-only.
//
// The ONLY edit to existing sources is the clearly-marked invoke block at the
// top of app_atualizar() in src/app.c (FIXTURE-Goal-C). No Tela routes, no
// AJ_* identifiers, no video-hole code is modified.
#ifndef NV_FIXTURE_H
#define NV_FIXTURE_H

#ifdef NV_FIXTURE
// Called every frame from app_atualizar(). Reads/consumes /tmp/nuvio-fixture.
void fixture_passo(void);
// Screen requested by the sequencer (-1 = none). Consumed by the caller.
int fixture_tela_pedida(void);
#endif

#endif
