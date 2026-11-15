#!/usr/bin/env bash
# Builds the TapTap T1000-E firmware and writes a flashable .uf2 to release/.
# Requires PlatformIO (pip install platformio).
set -euo pipefail
cd "$(dirname "$0")"
ENV=t1000e_taptap_ble
pio run -e "$ENV"
VERSION=$(grep -oE "FIRMWARE_VERSION='\"[^\"]+\"'" variants/t1000-e/platformio.ini | head -1 | sed -E "s/.*\"(.+)\".*/\1/")
mkdir -p release
python3 bin/uf2conv/uf2conv.py ".pio/build/$ENV/firmware.hex" -c -f 0xADA52840 -o "release/${ENV}-${VERSION}.uf2"
cp ".pio/build/$ENV/firmware.zip" "release/${ENV}-${VERSION}-ota.zip"
echo "Built release/${ENV}-${VERSION}.uf2"
