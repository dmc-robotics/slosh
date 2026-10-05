#!/bin/sh
# Compiles the shared gauge core to WebAssembly, embedded in tuner/slosh.js.
set -e
cd "$(dirname "$0")"
em++ -std=c++20 -O3 -I../src ../src/slosh/*.cpp bindings.cpp \
  -lembind -sMODULARIZE -sEXPORT_NAME=createSloshModule -sSINGLE_FILE -sALLOW_MEMORY_GROWTH \
  -o slosh.js
echo "Built tuner/slosh.js"
