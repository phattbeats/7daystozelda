#ifndef NETWORK_ANCHOR_WORLD_OBJECT_SYNC_H
#define NETWORK_ANCHOR_WORLD_OBJECT_SYNC_H
#ifdef __cplusplus

struct GetItemEntry;

// World-object sync (#4044): the pieces of the world the flag sync doesn't cover.
// - A hidden grotto one player bombs open (DoorAna, no flag) opens for everyone in the
//   room (WORLD_OBJECT).
// - A heart, ammo or magic drop (EnItem00) belongs to whoever picks it up: every client
//   has its own copy of a drop, so it is not also handed to the partners (GIVE_ITEM).
// - A Gold Skulltula token sets its flag the moment it is collected, so the partner's
//   copy disappears before they can collect it too.
// Sender, receiver and hooks live in Packets/WorldObject.cpp.
//
// Called from Anchor::RegisterHooks with the current connection state.
void RegisterWorldObjectHooks(bool isConnected);

// Per-frame: ages the personal-pickup window.
void WorldObjectTick();

// True when this item comes from picking up a heart/ammo/magic drop in the last few
// frames; OnItemReceive skips GIVE_ITEM for it.
bool WorldObject_IsPersonalPickup(const GetItemEntry& itemEntry);

#endif // __cplusplus
#endif // NETWORK_ANCHOR_WORLD_OBJECT_SYNC_H
