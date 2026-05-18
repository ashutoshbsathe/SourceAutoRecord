#!/bin/bash
# Formatting script for Harness features and POC client

# 1. Format C++ and Proto files with Google Style
# We explicitly list the source files to avoid formatting generated gRPC/PB files.
clang-format -style=google -i src/Features/Harness/*.cpp
clang-format -style=google -i src/Features/Harness/*.hpp
clang-format -style=google -i src/Features/Harness/*.proto

# 2. Format Python files
# Using uv to ensure ruff is available without global installation
uv tool run ruff format poc_client.py py/
