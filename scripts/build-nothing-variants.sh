#!/usr/bin/env bash
# Build one kernel and both panel-overlay variants, restoring the source panels.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
ARTIFACTS=${1:?Usage: bash scripts/build-nothing-variants.sh /path/to/artifacts}
export OUT=${OUT:-out}
case "$OUT" in
    /*) OUTPUT=$OUT ;;
    *) OUTPUT=$ROOT/$OUT ;;
esac
if [[ ! -f "$OUTPUT/.config" ]]; then
    echo "Configure $OUT with sweet_defconfig first; see Documentation/nothing-kernel.md." >&2
    exit 1
fi

PANELS=(
    arch/arm64/boot/dts/qcom/xiaomi/sweet/dsi-panel-k6-38-0c-0a-fhd-dsc-video.dtsi
    arch/arm64/boot/dts/qcom/xiaomi/sweet/dsi-panel-k6-38-0e-0b-fhd-dsc-video.dtsi
)
TEMPROOT=${TMPDIR:-/tmp/opencode}
[[ -d "$TEMPROOT" ]] || TEMPROOT=/tmp
BACKUP=$(mktemp -d "$TEMPROOT/nothing-panels.XXXXXX")
for i in "${!PANELS[@]}"; do
    cp -p -- "$ROOT/${PANELS[$i]}" "$BACKUP/$i"
done
restore_panels() {
    for i in "${!PANELS[@]}"; do
        cp -p -- "$BACKUP/$i" "$ROOT/${PANELS[$i]}"
    done
    rm -rf -- "$BACKUP"
}
trap restore_panels EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

set_panels() {
    python3 - "$ROOT" "$1" "$2" "${PANELS[@]}" <<'PY'
from pathlib import Path
import re
import sys

root, width, height, *files = sys.argv[1:]
updates = []
for name in files:
    path = Path(root) / name
    text = path.read_text()
    for dimension, value in (("width", width), ("height", height)):
        pattern = rf"(qcom,mdss-pan-physical-{dimension}-dimension\s*=\s*<)\d+(>;)"
        text, count = re.subn(pattern, lambda m: m[1] + value + m[2], text)
        if count != 1:
            raise RuntimeError(f"Expected one {dimension} property in {path}")
    updates.append((path, text))
for path, text in updates:
    path.write_text(text)
PY
}

mkdir -p -- "$ARTIFACTS/aosp" "$ARTIFACTS/oem"
LOGS=$(dirname -- "$ARTIFACTS")/logs
mkdir -p -- "$LOGS"
set_panels 69 156
bash "$ROOT/scripts/build-nothing.sh" 2>&1 | tee "$LOGS/build-aosp-final.log"
cp -- "$OUTPUT/arch/arm64/boot/"{Image.gz,dtb.img,dtbo.img} "$ARTIFACTS/aosp/"
cp -- "$OUTPUT/.config" "$ARTIFACTS/kernel.config"
cp -- "$OUTPUT/"{vmlinux,System.map} "$ARTIFACTS/"
cp -- "$OUTPUT/include/config/kernel.release" "$ARTIFACTS/"
cp -- "$OUTPUT/include/generated/compile.h" "$ARTIFACTS/"

set_panels 695 1546
bash "$ROOT/scripts/build-nothing.sh" dtb.img dtbo.img 2>&1 | tee "$LOGS/build-oem-final.log"
cp -- "$ARTIFACTS/aosp/Image.gz" "$ARTIFACTS/oem/"
cp -- "$OUTPUT/arch/arm64/boot/"{dtb.img,dtbo.img} "$ARTIFACTS/oem/"
