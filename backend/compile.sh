#!/bin/bash
g++ -std=c++17 -I/usr/local/include -o unibot backend/main.cpp -lpthread
echo "Compiled. Run using ./unibot"
