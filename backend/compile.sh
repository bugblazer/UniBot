#!/usr/bin/env bash
# Builds the UniBot server. Works on Linux, macOS and Windows (Git Bash + MinGW g++).
set -euo pipefail
cd "$(dirname "$0")/.."

[ -f backend/third_party/asio.hpp ] || bash backend/setup.sh

OUT="unibot"
EXTRA=""
LIBS="-lpthread"
case "$(uname -s)" in
  MINGW* | MSYS* | CYGWIN*)
    OUT="unibot.exe"
    # Static runtime: Git Bash ships an older libstdc++-6.dll that would otherwise load first and crash.
    EXTRA="-D_WIN32_WINNT=0x0A00 -static"
    LIBS="-lws2_32 -lmswsock -lpthread"
    ;;
esac

g++ -std=c++17 -O2 -DASIO_STANDALONE $EXTRA -Ibackend/third_party -o "$OUT" backend/main.cpp $LIBS
echo "Compiled. Start the server from the repo root with ./$OUT, then open http://localhost:18080"
