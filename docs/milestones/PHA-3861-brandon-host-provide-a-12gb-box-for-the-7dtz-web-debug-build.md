# PHA-3861: [Brandon/host] Provide a 12GB+ box for the 7DtZ web debug build (PHA-3860)

Status at export (2026-10-03): done

PHA-3860 tracks three web-build bugs: the ROM extractor abort, the palette corruption, and the OOB trap on player join. None of them can move without a debug rebuild, and that needs about 12 GB of RAM plus swap. PHATT-RAID cannot take it.

Action: either run the web build per src/BUILD-WEB.md with `-sASSERTIONS=1 -g2 -sSAFE_HEAP=1 -fexceptions` on your PC and share the output, or name a build host Vision Quest can use. Then mark this done.
