#!/usr/bin/env bash
# Builds the signed .hap files into dist/ and verifies their signatures.
#
# Needs signing to be set up first (DevEco Studio > File > Project Structure > Signing Configs >
# "Automatically generate signature", see README), and the DevEco tools (hvigorw, java) on PATH.
# DEVECO_SDK_HOME must point at the SDK folder (DevEco sets it; default below is the usual install).
set -euo pipefail
cd "$(dirname "$0")/.."

OUT=entry/build/default/outputs/default
SDK="${DEVECO_SDK_HOME:-F:/huwaei/DevEco Studio/sdk}"
SIGN_TOOL="$SDK/default/openharmony/toolchains/lib/hap-sign-tool.jar"
mkdir -p dist build-host

for mode in debug release; do
  echo "== building $mode"
  hvigorw clean --no-daemon > /dev/null
  hvigorw assembleHap --mode module -p product=default -p module=entry@default -p buildMode=$mode --no-daemon \
    > "build-host/signed-$mode.log" 2>&1 || { tail -20 "build-host/signed-$mode.log"; exit 1; }
  if [ ! -f "$OUT/entry-default-signed.hap" ]; then
    echo "No signed .hap was produced: set up signing in DevEco Studio first (see README)." >&2
    exit 1
  fi
  cp "$OUT/entry-default-signed.hap" "dist/safe-n-sound-$mode-signed.hap"
done

for mode in debug release; do
  echo "== verifying $mode"
  java -jar "$SIGN_TOOL" verify-app -inFile "dist/safe-n-sound-$mode-signed.hap" \
    -outCertChain "build-host/chain-$mode.cer" -outProfile "build-host/profile-$mode.p7b" 2>&1 | grep -E "verify-app|Verify"
done
echo "== done"
sha256sum dist/*.hap
