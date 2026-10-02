#pragma once
#include "SevenDays.h"

/**
 * Shared plumbing between the SevenDays modules (SevenDays.cpp owns it): who
 * decides room-wide state, and how JSON packets reach the room or one player.
 */
namespace SevenDays::Net {

bool Connected();
uint32_t ActingOwner(); // the room owner, or the lowest ready client while it is away
bool IsOwner();         // true when solo
uint32_t OwnId();       // 0 when solo
void SendTo(uint32_t clientId, nlohmann::json payload);
void Broadcast(nlohmann::json payload);
double Now();

// The material pool (kits live there too). Owner only: mutate, then broadcast.
PoolState& MutablePool();
void BroadcastPool();

} // namespace SevenDays::Net
