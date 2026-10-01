#!/usr/bin/env bash
# Downloads the standalone Asio headers Crow needs (header-only, not committed).
# Crow itself is vendored in backend/third_party/crow_all.h.
set -euo pipefail
cd "$(dirname "$0")/third_party"

ASIO_TAG="asio-1-28-2" # the bundled Crow uses io_service, which newer Asio releases removed
if [ -f asio.hpp ]; then
  echo "Asio already present."
  exit 0
fi

echo "Downloading Asio ($ASIO_TAG)..."
curl -fsSL "https://github.com/chriskohlhoff/asio/archive/refs/tags/${ASIO_TAG}.tar.gz" -o asio.tar.gz
tar -xzf asio.tar.gz --strip-components=3 "asio-${ASIO_TAG}/asio/include/asio" "asio-${ASIO_TAG}/asio/include/asio.hpp"
rm asio.tar.gz
echo "Asio installed in backend/third_party."
