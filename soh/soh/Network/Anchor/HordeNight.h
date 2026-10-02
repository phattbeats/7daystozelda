#ifndef NETWORK_ANCHOR_HORDE_NIGHT_H
#define NETWORK_ANCHOR_HORDE_NIGHT_H

/**
 * HordeNight (M3: horde-night spawner)
 *
 * Every Nth night (default 3rd) in an outdoor horde scene (Hyrule Field, Lon Lon
 * Ranch), the enemy authority spawns waves of undead around the living players
 * until dawn. Spawns go through plain Actor_Spawn at runtime, so EnemySync's
 * dynamic-spawn path assigns each one a host key and replicates it to every
 * mirror (ENEMY_SPAWN), exactly like vanilla En_Encount1 Stalchildren. SoH lets
 * runtime spawns use objects the scene didn't load (assets resolve by path), so
 * ReDeads and Gibdos can walk into Hyrule Field.
 *
 *  - Only the authority runs the spawner; mirrors just receive the enemies.
 *    After an authority handover the new authority picks the night up from its
 *    own clock and the live enemy count (no per-spawner bookkeeping to lose).
 *  - The cap counts every ENEMY-category actor in the scene, like the enemy
 *    randomizer's field spawner does.
 *  - Spawns ring a random living player (local or remote) at 250-450 units,
 *    floor-raycast, skipped on cliffs/water. Composition escalates each horde.
 *  - Dawn: spawning stops; leftover horde-only types (ReDead/Gibdo, Wolfos) are
 *    removed. Stalchildren burrow at dawn on their own.
 *  - HORDE_EVENT tells same-scene peers when a horde starts and ends.
 *
 * CVars (all under gRemote.Anchor.):
 *   HordeNight          0/1  master switch (default 0: opt-in game mode)
 *   HordeInterval       int  every Nth night (default 3)
 *   HordeNightForce     0/1  treat now as a horde night (testing)
 *   HordeMaxAlive       int  base enemy cap (default 10; +2 per horde, max 24)
 *   HordeSpawnFrames    int  frames between spawns (default 30 = 1.5 s)
 *
 * 7 Days to Zelda M6 (gSevenDays.Raids) turns hordes into raids on a base
 * (soh/SevenDays/Raids.cpp). While it is on, this spawner stands down and the
 * raid director drives the night with the helpers below: the same cap, the same
 * living-player set, and ShambleToward, the generalized ShambleTowardPlayers
 * (a leashed enemy's home walks toward any goal: the workbench, or a player).
 */

#ifdef __cplusplus
#include <cstdint>
#include <memory> // must precede extern "C": z64.h pulls in <memory> under C++
#include <vector>
extern "C" {
#include "z64.h"
}

namespace HordeNight {

bool Enabled();

// Authority-side spawner tick; called from EnemySync::PerFrameTick after Tick.
void PerFrameTick();

// Scene teardown: forget "a horde is active" without announcing an end.
void Reset();

// Which horde this is (1-based), or 0 when the current night isn't a horde night.
int32_t CurrentHordeNumber();

// Shared with the raid director.
constexpr f32 HORDE_SHAMBLE_SPEED = 1.2f; // units per frame
std::vector<Actor*> LivingPlayers();      // local Link (if alive) + same-scene living puppets
Actor* NearestLivingPlayer(const Vec3f& from, f32* outDistXZ);
void ShambleToward(Actor* a, const Vec3f& goal, f32 speed);
int32_t MaxAlive(int32_t horde);
int32_t SpawnFrames();

} // namespace HordeNight
#endif // __cplusplus

#endif // NETWORK_ANCHOR_HORDE_NIGHT_H
