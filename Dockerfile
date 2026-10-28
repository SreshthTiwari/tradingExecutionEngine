# Build and test in one stage, ship only the binaries.
FROM ubuntu:24.04 AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests
COPY benchmarks ./benchmarks
COPY tools ./tools
COPY data ./data
# the build fails if any test fails
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel \
    && ctest --test-dir build --output-on-failure

FROM ubuntu:24.04
RUN useradd --system --no-create-home engine
COPY --from=build /src/build/trading_system /usr/local/bin/trading_system
COPY --from=build /src/build/replay /usr/local/bin/replay
USER engine
EXPOSE 9000
# 0.0.0.0 so the published port is reachable from outside the container
ENTRYPOINT ["trading_system"]
CMD ["serve", "9000", "0.0.0.0"]
