# Build stage: compile the server (Asio headers are fetched by setup.sh).
FROM gcc:14 AS build
WORKDIR /src
COPY backend ./backend
RUN bash backend/setup.sh \
 && g++ -std=c++17 -O2 -DASIO_STANDALONE -Ibackend/third_party \
      -static-libstdc++ -static-libgcc -o unibot backend/main.cpp -lpthread

# Runtime stage: small image with just the binary, the web UI and default data.
FROM debian:bookworm-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends tzdata \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/unibot ./unibot
COPY webgui ./webgui
COPY backend/data ./backend/data
EXPOSE 18080
# Mount a volume over /app/backend/data to keep admin edits across rebuilds.
CMD ["./unibot"]
