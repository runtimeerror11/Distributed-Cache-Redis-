FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDISTCACHE_BUILD_TESTS=OFF \
    && cmake --build build -j

FROM ubuntu:24.04
COPY --from=build /src/build/cache_node /src/build/coordinator /usr/local/bin/
