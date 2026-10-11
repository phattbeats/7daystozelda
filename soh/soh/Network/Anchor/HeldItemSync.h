#ifndef NETWORK_ANCHOR_HELD_ITEM_SYNC_H
#define NETWORK_ANCHOR_HELD_ITEM_SYNC_H
#ifdef __cplusplus

// Held and thrown item sync (#4180): bombs, bombchus and carried/thrown pots, crates,
// rocks and bushes show on the partner's screen, in the puppet's hands and in flight.
// Sender, receiver and the "ghost" actors live in Packets/HeldItemSync.cpp.
//
// Called from Anchor::RegisterHooks with the current connection state.
void RegisterHeldItemSyncHooks(bool isConnected);

// Per-frame: streams the items this client owns (HELD_ITEMS) and keeps the partner
// ghosts in step with the latest stream.
void HeldItemSyncTick();

#endif // __cplusplus
#endif // NETWORK_ANCHOR_HELD_ITEM_SYNC_H
