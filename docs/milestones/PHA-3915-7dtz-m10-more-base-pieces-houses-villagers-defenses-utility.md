# PHA-3915: 7DtZ M10: more base pieces: houses, villagers, defenses, utility

Status at export (2026-10-03): done

Brandon asked on PHA-3870 (2026-10-03): "any other crafting or base items we could add? like maybe village houses or something?" This is the candidate list. Brandon picks which ones to build; the rest get cut.

**Limits that apply to every piece.** A base holds at most about 24 pieces, because of the scene's dynamic-collision budget. Art comes from Ocarina of Time or Majora's Mask models only. Each piece is a new ActorDB placeable like the M5 pieces. OoT's houses are part of each scene's ground mesh, not separate models. A "house" would therefore be assembled from existing pieces (crates, fences, signs, roof planks) or reuse a standalone model such as a tent or hut, if the ROM has one that fits.

**Shelter and village**
- **Hut / house.** A respawn point for the base after a game-over, and it stops raid damage to anything inside it. Each house gives the base +1 villager slot.
- **Villagers.** Rescued NPCs (refugee Kokiri, Kakariko carpenters) move into houses. Each gives a small material trickle at dawn and has a line about the last raid. Villagers hide indoors on raid nights. This ties in with PHA-3913.
- **Bed / campfire.** On a non-raid night, everyone sleeps to dawn, like the Sun's Song. Also heals a little.
- **Flag in your tunic color.** Marks the base on the map. Each player can plant one.

**Defense**
- **Watchtower / platform plus ladder.** High ground for slingshot and bow, and walls you can stand on.
- **Scarecrow decoy.** Enemies go for it first, buying time.
- **Deku Baba planter.** A living turret that bites anything that comes close. Unlocked after the Deku Tree.
- **Cucco coop.** Cuccos swarm enemies that hit the base, the way they swarm Link in vanilla.
- **Alarm bell.** Rings for every player in the room when the base takes damage while nobody is there.
- **Lon Lon fence and horse jump.** Cheap long walls, built from ranch art.

**Utility**
- **Cooking pot.** Turns Rot, Fiber and Bone into potions (red, then green).
- **Fairy shrine.** Heals players in range once per night.
- **Repair bench.** Slowly repairs walls between raids.
- **Silo / bigger chest.** Raises how much of each material the pool can hold, if we add a cap.

**Done when:** Brandon has picked the pieces, the picked ones are built, and they are verified in the live game in a browser with screenshots.

