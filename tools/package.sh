#!/bin/bash
# Assemble the SD card layout from a finished Quartus build:
#   dist/ + src/fpga/output_files/ap_core.rbf -> release/NetTerm_Pocket_<version>.zip
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RBF="$ROOT/src/fpga/output_files/ap_core.rbf"
CORE="$ROOT/dist/Cores/Miasmark.NetTerm"
VERSION=$(python3 -c "import json;print(json.load(open('$CORE/core.json'))['core']['metadata']['version'])")

[ -f "$RBF" ] || { echo "missing $RBF - build first"; exit 1; }
# core.json's length limits (Analogue's core.json docs): the Pocket says
# "Cannot find bitstream" when cores[].filename is longer than 15 characters.
python3 - "$CORE/core.json" <<'PY'
import json, sys
c = json.load(open(sys.argv[1]))["core"]
m = c["metadata"]
bad = [(k, len(m[k]), n) for k, n in (("shortname", 31), ("description", 63), ("author", 31),
       ("version", 31), ("url", 63), ("date_release", 10)) if len(m[k]) > n]
bad += [("cores[].filename " + x["filename"], len(x["filename"]), 15) for x in c["cores"] if len(x["filename"]) > 15]
bad += [("cores[].name " + x["name"], len(x["name"]), 15) for x in c["cores"] if len(x.get("name", "")) > 15]
for k, l, n in bad:
    print("core.json: %s is %d characters, the limit is %d" % (k, l, n))
sys.exit(1 if bad else 0)
PY
python3 "$ROOT/tools/reverse_bits.py" "$RBF" "$CORE/netterm.rbf_r"

mkdir -p "$ROOT/release"
OUT="$ROOT/release/NetTerm_Pocket_${VERSION}.zip"
rm -f "$OUT"
# License texts travel with the bitstream: ours, PicoRV32's, and Analogue's APF license
# (it lives only in the source headers; copied word for word).
LIC="$CORE/licenses"
mkdir -p "$LIC"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_NOTICES.md" "$LIC/"
cp "$ROOT/src/fpga/core/third_party/picorv32/COPYING" "$LIC/PicoRV32-ISC-LICENSE.txt"
awk '{ sub(/\r$/, "") } /^$/ { print; next } /^\/\/ *$/ || !/^\/\// { exit } { sub(/^\/\/ ?/, ""); print }' \
	"$ROOT/src/fpga/apf/apf_top.v" > "$LIC/Analogue-APF-Software-License.txt"
(cd "$ROOT/dist" && python3 - "$OUT" <<'PY'
import os, sys, zipfile
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as z:
    for top in ("Cores", "Platforms"):
        for d, _, files in os.walk(top):
            z.write(d)
            for f in files:
                if f != ".keep":
                    z.write(os.path.join(d, f))
PY
)
echo "$OUT"
