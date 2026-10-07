# Web build tree (for agents)

`zelda-buildtree.tar.gz` (Nextcloud: PHATT-TECH/Projects/7daystozelda) holds:

- `/root/OOT-True-Co-op`: the repo with all patches committed (git history intact), its
  submodules on their web branches, and `build-web/` with every object already compiled,
  including the randomizer tables that need 12 GB of RAM to build from scratch.
- `/root/emsdk`: emsdk 3.1.64 with the SDL2/ogg/vorbis/libpng/zlib ports already fetched.
- `/root/stb_good.h`: a known-good `stb_image.h` (CMake re-runs sometimes fetch an empty one).

The paths must stay exactly as above: the ninja files are absolute, and a moved tree
rebuilds from zero. The Dockerfile here does that for you.

## Use

```sh
cp /path/to/zelda-buildtree.tar.gz .
docker build -t zelda-build .
docker run --rm -v "$PWD/out:/out" zelda-build          # build, export to ./out/public-update
docker run --rm -it -v "$PWD/out:/out" zelda-build bash # edit code inside, then run build-web
```

Keep work: commit inside the container and `git format-patch` to `/out`, or mount a
volume over `/root/OOT-True-Co-op` after the first extract.

## Cost

- A change in one source file is a recompile of that file plus a ~30-60 s link. That needs about 4 GB of RAM, no swap.
- `JOBS=1` lowers peak memory if the host is tight.
- Touching the randomizer table sources, or anything they include, triggers the 12 GB rebuild. Avoid that unless planned.

## Rules (from #3860)

- No `std::thread` on web.
- Packets carry JSON, never raw structs.
- Test with `tools/webtest/` (in the bundle) before shipping.
- `shell.html` changes need a relink; `build-web` forces one every run.
