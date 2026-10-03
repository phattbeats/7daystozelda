# The web build tree, frozen at the exact paths it was built in, so builds are
# incremental (a 30-60 s relink) instead of a from-scratch 12 GB build.
#   1. Put zelda-buildtree.tar.gz next to this file.
#   2. docker build -t zelda-build .
#   3. docker run --rm -v "$PWD/out:/out" zelda-build            # build + export
#      docker run --rm -it -v "$PWD/out:/out" zelda-build bash   # work inside
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      cmake ninja-build python3 git ca-certificates xz-utils gzip patch \
    && rm -rf /var/lib/apt/lists/*
# ADD unpacks a local .tar.gz straight into the image: one layer, no second copy.
ADD zelda-buildtree.tar.gz /
COPY build-web.sh /usr/local/bin/build-web
RUN chmod +x /usr/local/bin/build-web
WORKDIR /root/OOT-True-Co-op
CMD ["build-web"]
