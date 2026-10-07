#ifndef NETWORK_ANCHOR_PUSH_BLOCK_SYNC_H
#define NETWORK_ANCHOR_PUSH_BLOCK_SYNC_H
#ifdef __cplusplus

// Push-block sync (#4020): a block one player pushes (ObjOshihiki) moves on every
// partner's screen in the same room, and a player walking into a room gets the
// partner's already-pushed blocks. Sender, receiver and pending queue live in
// Packets/PushBlock.cpp.
//
// Called from Anchor::RegisterHooks with the current connection state.
void RegisterPushBlockHooks(bool isConnected);

// Per-frame: retries pushes that arrived while the block was busy (mid-push / falling /
// not spawned yet) and asks the partners for this room's blocks on room entry.
void PushBlockTick();

#endif // __cplusplus
#endif // NETWORK_ANCHOR_PUSH_BLOCK_SYNC_H
