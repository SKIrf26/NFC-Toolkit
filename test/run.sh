#!/usr/bin/env bash
# Host unit tests for the NDEF encoder/decoder (no hardware needed).
set -euo pipefail
cd "$(dirname "$0")"
g++ -std=c++17 -Wall -Wextra -I. -o /tmp/ndef_builder_test ndef_builder_test.cpp ../NFC_Toolkit/ndef_builder.cpp
/tmp/ndef_builder_test
