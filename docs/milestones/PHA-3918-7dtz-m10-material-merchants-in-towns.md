# PHA-3918: 7DtZ M10: material merchants in towns

Status at export (2026-10-03): done

Split out of PHA-3913 (that issue stays flavor text only, per Brandon on 2026-10-03). Scope came from Brandon's question on PHA-3870: "places to purchase materials?".

Today materials can only be gathered or sold (workbench Trade tab, SevenDaysData.cpp trade_*). Nothing sells them. Add buy points in towns through a new "Buy" talk option or a merchant placeable, never by editing vanilla shop actors. Price them above the Trade tab's sell rate, so selling and rebuying never makes a profit.
- Kokiri Forest: none, so the prologue stays gathering-first.
- Market bazaar / Kakariko: Wood, Fiber.
- Goron City: Stone, later Ore.
- Kakariko Graveyard (Dampé): Bone.
- Lon Lon Ranch (Talon): Wood, Fiber in bulk.
- Zora / Gerudo: later-tier materials.

Must not break or overwrite vanilla dialogue, shops or quest flow (Brandon's bar on PHA-3913). The PHA-3913 patch has a ROM message-table check (no choice/event/item codes) worth reusing if any vanilla text is touched.

Done when: one merchant per listed town sells at least one material, the purchase goes through the room owner like CRAFT_REQUEST, and it is verified in the live game with screenshots.
