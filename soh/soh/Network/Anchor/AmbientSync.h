#ifndef NETWORK_ANCHOR_AMBIENT_SYNC_H
#define NETWORK_ANCHOR_AMBIENT_SYNC_H
#ifdef __cplusplus

// Ambient-actor sync (#4042): the critters and walkers that wander a town on their
// own — Kakariko's cuccos, the market dogs, the carpenters, castle guards, butterflies,
// Lon Lon's corral horses — are in the same place on every partner's screen.
// Sender, receiver and the actor list live in Packets/AmbientSync.cpp.
//
// Called from Anchor::RegisterHooks with the current connection state.
void RegisterAmbientSyncHooks(bool isConnected);

// Per-frame: streams the actors this client drives (AMBIENT_STATE). Runs after
// EnemySync::PerFrameTick, whose scene authority it reuses.
void AmbientSyncTick();

#endif // __cplusplus
#endif // NETWORK_ANCHOR_AMBIENT_SYNC_H
