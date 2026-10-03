# PHA-3913: 7DtZ M9: the world reacts — towns and NPCs talk about the nights

Status at export (2026-10-03): done

Today only Kokiri Forest has new dialogue (Fado, Saria, a Kokiri boy, the Day 1 sign; Placeables.cpp sVillageLines) and NPC behavior (Kokiri hide on raid nights). Every other town is vanilla.

Scope (CustomMessageManager, same pattern as sVillageLines, gated by gSevenDays.Enabled and save state):
- Lines that change with progress/raids survived in Kakariko (Impa/Anju/carpenters, watchtower), Lon Lon Ranch (Malon/Talon about barricading the ranch), Market + Castle Town guards (rumors from the field), Zora Domain, Goron City, Gerudo Fortress, Lake Hylia (lab scientist), Death Mountain.
- A few towns boarded up after their raids start (seeded placeables, like the Kokiri village), adult era ruins notes.
- Townsfolk stay indoors on raid nights where the scene supports it.
- Gossip stones / Navi C-up pool reference the current base and next raid.

Done when: each listed town has at least 2 changed lines reflecting the nights, verified in the live game in a browser with screenshots.
