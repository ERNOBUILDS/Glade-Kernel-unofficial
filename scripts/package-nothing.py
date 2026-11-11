#!/usr/bin/env python3
"""Package saved AOSP/OEM images with a pinned AnyKernel3 template."""

import argparse
import datetime
import gzip
import hashlib
import json
from pathlib import Path
import re
import stat
import subprocess
import zipfile


AK3_REVISION = "d93ab7bab62d7ecff359bfb9cc64dcbfb1afb017"
ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", type=Path, required=True)
    parser.add_argument("--artifacts", type=Path,
                        help="legacy shared payloads; not allowed for oc1")
    parser.add_argument("--oc-artifacts", type=Path,
                        help="OC directory containing aosp/ and oem/ images")
    parser.add_argument("--nooc-artifacts", type=Path,
                        help="preserved NoOC directory containing aosp/ and oem/ images")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--date", default=datetime.date.today().strftime("%Y%m%d"))
    parser.add_argument("--revision", default="",
                        help="optional release suffix, for example bootfix1")
    args = parser.parse_args()
    split = args.oc_artifacts is not None or args.nooc_artifacts is not None
    if split:
        if args.artifacts or not (args.oc_artifacts and args.nooc_artifacts):
            parser.error("supply both --oc-artifacts and --nooc-artifacts, not --artifacts")
        sources = {"OC": args.oc_artifacts, "NoOC": args.nooc_artifacts}
    else:
        if not args.artifacts or args.revision == "oc1":
            parser.error("oc1 requires separate OC and preserved NoOC artifacts")
        sources = {"OC": args.artifacts, "NoOC": args.artifacts}
    date = datetime.datetime.strptime(args.date, "%Y%m%d")
    if args.revision and not re.fullmatch(r"[a-z0-9][a-z0-9-]*", args.revision):
        parser.error("revision must contain only lowercase letters, digits and hyphens")
    suffix = "-" + args.revision if args.revision else ""
    revision = subprocess.check_output(
        ["git", "-C", str(args.template), "rev-parse", "HEAD"], text=True).strip()
    if revision != AK3_REVISION:
        parser.error(f"AnyKernel3 must be at {AK3_REVISION}, got {revision}")

    # Read tracked template content from git, excluding local captures/build files.
    names = subprocess.check_output(
        ["git", "-C", str(args.template), "ls-tree", "-r", "--name-only", "HEAD"],
        text=True).splitlines()
    template = {
        name: subprocess.check_output(
            ["git", "-C", str(args.template), "show", f"HEAD:{name}"])
        for name in names
        if name == "LICENSE" or name.startswith(("META-INF/", "tools/"))
    }
    script = (ROOT / "packaging/anykernel.sh").read_text()
    for marker in ("NOTHING_OC=1;", "NOTHING_VARIANT=AOSP;"):
        if script.count(marker) != 1:
            parser.error(f"expected exactly one installer marker: {marker}")
    banner = (ROOT / "packaging/banner").read_bytes()
    if (script.count("ui_print ERNOBUILDS;") != 1 or
            "kernel.string=Nothing kernel\n" not in script or
            "glade" in script.lower() or b"glade" in banner.lower()):
        parser.error("installer must show ERNOBUILDS / Nothing kernel, never glade")
    images = {}
    payload_hashes = {}
    for mode, source in sources.items():
        images[mode] = {}
        for variant in ("AOSP", "OEM"):
            payloads = images[mode][variant] = {}
            for name in ("Image.gz", "dtb.img", "dtbo.img"):
                data = (source / variant.lower() / name).read_bytes()
                if not data:
                    parser.error(f"empty {mode}/{variant} artifact: {name}")
                payloads[name] = data
            raw_image = gzip.decompress(payloads["Image.gz"])
            payload_hashes[f"{variant}-{mode}"] = {
                name: hashlib.sha256(data).hexdigest()
                for name, data in {**payloads, "Image": raw_image}.items()
            }
        if images[mode]["AOSP"]["Image.gz"] != images[mode]["OEM"]["Image.gz"]:
            parser.error(f"{mode}: AOSP/OEM must use the same kernel")
    if split:
        for variant in ("AOSP", "OEM"):
            oc, nooc = images["OC"][variant], images["NoOC"][variant]
            if gzip.decompress(oc["Image.gz"]) == gzip.decompress(nooc["Image.gz"]):
                parser.error(f"{variant}: equal OC/NoOC Image is a failed OC build")
            if oc["dtb.img"] == nooc["dtb.img"]:
                parser.error(f"{variant}: OC DT policy must differ from preserved NoOC DTB")
            if (b"nothing,oc-target-khz\0" not in oc["dtb.img"] or
                    b"nothing,oc-target-khz\0" in nooc["dtb.img"]):
                parser.error(f"{variant}: expected functional OC-only DT properties")

    args.output.mkdir(parents=True, exist_ok=True)
    names = [f"Nothing-kernel-sweet-{variant}-{mode}-{args.date}{suffix}.zip"
             for variant in ("AOSP", "OEM") for mode in ("OC", "NoOC")]
    if any((args.output / name).exists() for name in names):
        parser.error("refusing to overwrite an existing release ZIP")
    checksums = []
    for variant in ("AOSP", "OEM"):
        for mode, enabled in (("OC", "1"), ("NoOC", "0")):
            content = dict(template)
            content.update(images[mode][variant])
            content["banner"] = banner
            content["tools/nothing-cmdline.sh"] = (
                ROOT / "packaging/nothing-cmdline.sh").read_bytes()
            content["anykernel.sh"] = script.replace(
                "NOTHING_OC=1;", f"NOTHING_OC={enabled};").replace(
                "NOTHING_VARIANT=AOSP;", f"NOTHING_VARIANT={variant};").encode()
            name = f"Nothing-kernel-sweet-{variant}-{mode}-{args.date}{suffix}.zip"
            output = args.output / name
            with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                                 compresslevel=9) as archive:
                for path, data in sorted(content.items()):
                    entry = zipfile.ZipInfo(path, date.timetuple()[:6])
                    executable = (path.startswith("tools/") or path == "anykernel.sh"
                                  or path.endswith("/update-binary"))
                    entry.create_system = 3
                    entry.external_attr = (stat.S_IFREG | (0o755 if executable else 0o644)) << 16
                    archive.writestr(entry, data, compress_type=zipfile.ZIP_DEFLATED,
                                     compresslevel=9)
            with zipfile.ZipFile(output) as archive:
                if archive.testzip() is not None:
                    raise RuntimeError(f"corrupt archive: {output}")
            digest = hashlib.sha256(output.read_bytes()).hexdigest()
            checksums.append(f"{digest}  {name}\n")
            print(f"{name}: {output.stat().st_size} bytes, SHA256 {digest}")
    (args.output / "SHA256SUMS").write_text("".join(checksums))
    (args.output / "PAYLOAD-SHA256.json").write_text(
        json.dumps(payload_hashes, indent=2) + "\n")


if __name__ == "__main__":
    main()
