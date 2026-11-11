#!/usr/bin/env python3
"""Run the actual OC driver functions against mock MMIO, natively and on AArch64."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = (
    "clk_osm_read_lut_row", "clk_osm_lut_row_matches",
    "nothing_oc_print_row", "nothing_oc_dump_lut", "clk_osm_program_lut_row",
    "clk_osm_oc_apply", "nothing_oc_can_request", "nothing_oc_revoke",
    "nothing_oc_select_index", "nothing_oc_request_index",
    "osm_cpufreq_target", "osm_cpufreq_verify", "osm_cpufreq_fast_switch",
    "osm_cpufreq_get", "osm_cpufreq_cpu_init", "osm_cpufreq_cpu_exit",
)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--toolchain", type=Path,
                        default=ROOT.parent / "nothing-toolchain")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    driver = ROOT / "drivers/clk/qcom/clk-cpu-osm.c"
    text = driver.read_text()
    extracted = []
    for name in FUNCTIONS:
        pattern = (r"^static[ \t\w*]*\n?[ \t]*" + re.escape(name) +
                   r"\([^;]*?\)\n\{.*?^\}")
        match = re.search(pattern, text, re.MULTILINE | re.DOTALL)
        if not match:
            raise RuntimeError(f"cannot extract actual driver function {name}")
        extracted.append(match[0])
    (args.output / "model-driver.h").write_text("\n\n".join(extracted) + "\n")
    harness = ROOT / "scripts/nothing-oc-selftest.c"
    common = ["-std=gnu11", "-DCONFIG_NOTHING_OC=1", "-Wall", "-Wextra",
              "-Wno-unused-parameter", "-Wno-unused-but-set-variable",
              "-I" + str(ROOT / "drivers/clk/qcom"), "-I" + str(args.output)]
    native = args.output / "oc-native-test"
    arm = args.output / "oc-aarch64-test"
    commands = [
        ["gcc", *common, "-O1", "-g", "-fsanitize=address,undefined",
         "-fno-omit-frame-pointer", str(harness), "-o", str(native)],
        [str(args.toolchain / "clang/bin/clang"), *common,
         "--target=aarch64-linux-gnu", "-fuse-ld=lld", "-nostdlib", "-static",
         "-fno-builtin", "-O2", "-Wl,-e,_start", str(harness), "-o", str(arm)],
        [str(native)],
        [str(args.toolchain / "host/bin/qemu-aarch64-static"), str(arm)],
    ]
    logs = []
    for command in commands:
        result = subprocess.run(command, text=True, capture_output=True, check=True)
        logs.append({"command": command, "stdout": result.stdout,
                     "stderr": result.stderr, "returncode": result.returncode})
        if result.stdout:
            print(result.stdout, end="")
        if result.stderr:
            print(result.stderr, end="")
    report = {"driver_sha256": hashlib.sha256(driver.read_bytes()).hexdigest(),
              "functions_from_actual_driver": list(FUNCTIONS), "runs": logs,
              "scope": "mock MMIO/thermal/CPUFreq logic; NOT a phone/kernel boot"}
    (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
