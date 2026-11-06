#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Build RELAY-16 for the FM-1 and the SMK-37 (Pro, Elite, MKE-P37) with fm1-base (base/), and collect the packages
# in build/relay16/ with their SHA256SUMS. macOS runs the JieLi toolchain in Docker (base/tools/build.py).
#   ./build.sh                      development builds
#   ./build.sh --release 0.1        release builds (the Elite keeps a development identity: see README)
#   ./build.sh --host               the host simulators, then the tests in app/tests/
#   JIELI_TOOLCHAIN  JieLi's Linux toolchain (default ~/.jieli/toolchain)
#   AC79_SDK         JieLi's AC79 SDK (default ~/.jieli/ac79-sdk)
#   PYTHON           a Python 3 with Pillow (default python3.11 if there is one)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/.jieli/ac79-sdk}"
if [ -z "$PYTHON" ]; then
    if command -v python3.11 >/dev/null 2>&1; then PYTHON=python3.11; else PYTHON=python3; fi
fi
if [ "$1" = "--host" ]; then
    FM1B_BOARD=fm1 "$PYTHON" base/tools/build.py app --host
    FM1B_BOARD=smk37 FM1B_MODEL=pro "$PYTHON" base/tools/build.py app --host
    ls app/build/host/
    for exe in app/build/host/relay16_host; do "$exe" app/tests/basic.txt app/build/host; done
    for exe in app/build/host/relay16-smk37*_host; do "$exe" app/tests/smk37.txt app/build/host; done
    exit 0
fi
REL=""
if [ "$1" = "--release" ]; then REL="--release $2"; fi
FM1B_BOARD=fm1 "$PYTHON" base/tools/build.py app $REL
FM1B_BOARD=smk37 FM1B_MODEL=pro "$PYTHON" base/tools/build.py app $REL
FM1B_BOARD=smk37 FM1B_MODEL=mke "$PYTHON" base/tools/build.py app $REL
FM1B_BOARD=smk37 FM1B_MODEL=elite "$PYTHON" base/tools/build.py app   # "SMK-37 Elite_73XXYYZZ" is 21 characters
mkdir -p build/relay16
rm -f build/relay16/*.fwsc
find app/build -maxdepth 3 -name '*.fwsc' -exec cp {} build/relay16/ \;
(cd build/relay16 && shasum -a 256 *.fwsc > SHA256SUMS && cat SHA256SUMS)
