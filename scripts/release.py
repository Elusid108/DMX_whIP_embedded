"""Build every release board and package one bundle for the companion.

  python scripts/release.py              build + package
  python scripts/release.py --no-build   package the existing .pio builds
  python scripts/release.py -e xiao-c3   only some envs (repeatable)

Release envs are the platformio.ini [env:*] sections with
`custom_release = yes`. Everything else comes from the built files, so there
is nothing to keep in sync by hand:

  board id + version + api   the image's WHIPFW:<board>:<ver>:<api>; tag
  chip, flash mode/freq/size the image header
  nvs / otadata / app        the built partitions.bin

Every tag must carry the version in include/version.h or the run fails.

Output: dist/whip-<ver>/<boardId>/{bootloader,partitions,firmware,
firmware.factory}.bin, dist/whip-<ver>/manifest.json, and dist/whip-<ver>.zip.
"""
import argparse
import configparser
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TAG_RE = re.compile(rb"WHIPFW:([A-Za-z0-9._-]{1,40}):([A-Za-z0-9._-]{1,40}):([0-9]{1,5});")

# esptool image header byte 12 (chip id) -> chip name.
CHIP_IDS = {0: "esp32", 2: "esp32s2", 5: "esp32c3", 9: "esp32s3", 12: "esp32c2",
            13: "esp32c6", 16: "esp32h2", 18: "esp32p4", 20: "esp32c61", 23: "esp32c5"}
# Where the ROM loads the second-stage bootloader.
BOOTLOADER_OFFSET = {"esp32": 0x1000, "esp32s2": 0x1000, "esp32c5": 0x2000,
                     "esp32p4": 0x2000, "esp32c61": 0x0}
FLASH_MODES = {0: "qio", 1: "qout", 2: "dio", 3: "dout"}
FLASH_SIZES = {0: "1MB", 1: "2MB", 2: "4MB", 3: "8MB", 4: "16MB", 5: "32MB",
               6: "64MB", 7: "128MB"}
# Header freq nibble differs per chip (see esptool targets).
FLASH_FREQS = {
    "default": {0x0: "40m", 0x1: "26m", 0x2: "20m", 0xF: "80m"},
    "esp32c6": {0x0: "80m", 0x2: "20m"},
    "esp32c2": {0x0: "60m", 0x1: "30m", 0x2: "20m", 0xF: "15m"},
    "esp32h2": {0x0: "48m", 0x2: "16m", 0xF: "12m"},
}
PARTITIONS_OFFSET = 0x8000


def fail(msg):
    print("release: " + msg, file=sys.stderr)
    sys.exit(1)


def fw_version():
    with open(os.path.join(ROOT, "include", "version.h"), encoding="utf-8") as f:
        m = re.search(r'#define\s+WHIP_FW_VERSION\s+"([^"]+)"', f.read())
    if not m:
        fail("no WHIP_FW_VERSION in include/version.h")
    return m.group(1)


def release_envs():
    cfg = configparser.ConfigParser(interpolation=None, strict=False)
    cfg.read(os.path.join(ROOT, "platformio.ini"), encoding="utf-8")
    out = []
    for section in cfg.sections():
        if not section.startswith("env:"):
            continue
        if cfg.get(section, "custom_release", fallback="").strip().lower() in ("yes", "true", "1"):
            out.append(section[4:])
    return out


def pio_cmd():
    for name in ("pio", "platformio"):
        found = shutil.which(name)
        if found:
            return [found]
    penv = os.path.join(os.path.expanduser("~"), ".platformio", "penv",
                        "Scripts" if os.name == "nt" else "bin")
    for name in ("pio.exe", "pio"):
        path = os.path.join(penv, name)
        if os.path.isfile(path):
            return [path]
    return [sys.executable, "-m", "platformio"]


def build(envs):
    cmd = pio_cmd() + ["run"]
    for env in envs:
        cmd += ["-e", env]
    print("release: " + " ".join(cmd), flush=True)
    if subprocess.call(cmd, cwd=ROOT) != 0:
        fail("pio run failed")


def read(path):
    with open(path, "rb") as f:
        return f.read()


def image_header(data):
    if len(data) < 24 or data[0] != 0xE9:
        fail("not an ESP image")
    chip = CHIP_IDS.get(struct.unpack_from("<H", data, 12)[0])
    if chip is None:
        fail("unknown chip id %d" % struct.unpack_from("<H", data, 12)[0])
    freqs = FLASH_FREQS.get(chip, FLASH_FREQS["default"])
    return {
        "chip": chip,
        "mode": FLASH_MODES.get(data[2], "dio"),
        "freq": freqs.get(data[3] & 0x0F, "keep"),
        "size": FLASH_SIZES.get(data[3] >> 4, "keep"),
    }


def partition_table(data):
    """{label: (type, subtype, offset, size)} from a built partitions.bin."""
    out = {}
    for i in range(0, len(data) - 31, 32):
        magic, ptype, sub, off, size = struct.unpack_from("<HBBII", data, i)
        if magic != 0x50AA:
            break
        label = data[i + 12:i + 28].split(b"\0", 1)[0].decode("ascii", "replace")
        out[label] = (ptype, sub, off, size)
    return out


def layout(parts):
    app = sorted(p for p in parts.values() if p[0] == 0x00)
    nvs = next((p for p in parts.values() if p[0] == 0x01 and p[1] == 0x02), None)
    otadata = next((p for p in parts.values() if p[0] == 0x01 and p[1] == 0x00), None)
    if not app:
        fail("partition table has no app partition")
    out = {"app": app[0][2], "appSize": app[0][3]}
    if nvs:
        out.update(nvs=nvs[2], nvsSize=nvs[3])
    if otadata:
        out.update(otadata=otadata[2], otadataSize=otadata[3])
    return out


def part_entry(role, rel, data, offset):
    return {"role": role, "file": rel, "offset": offset, "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def package_env(env, version, out_dir):
    build_dir = os.path.join(ROOT, ".pio", "build", env)
    fw_path = os.path.join(build_dir, "firmware.bin")
    if not os.path.isfile(fw_path):
        fail("%s: no firmware.bin (build it first)" % env)
    fw = read(fw_path)
    tags = TAG_RE.findall(fw)
    if len(tags) != 1:
        fail("%s: expected one WHIPFW tag, found %d" % (env, len(tags)))
    board, tag_ver, api = (t.decode() for t in tags[0])
    if tag_ver != version:
        fail("%s: image is v%s but version.h is v%s (rebuild)" % (env, tag_ver, version))
    boot = read(os.path.join(build_dir, "bootloader.bin"))
    ptab = read(os.path.join(build_dir, "partitions.bin"))
    head = image_header(boot)
    lay = layout(partition_table(ptab))
    if len(fw) > lay["appSize"]:
        fail("%s: firmware %d B does not fit the %d B app slot" % (env, len(fw), lay["appSize"]))

    board_dir = os.path.join(out_dir, board)
    os.makedirs(board_dir, exist_ok=True)
    files = [("bootloader", "bootloader.bin", boot, BOOTLOADER_OFFSET.get(head["chip"], 0)),
             ("partitions", "partitions.bin", ptab, PARTITIONS_OFFSET),
             ("app", "firmware.bin", fw, lay["app"])]
    factory = os.path.join(build_dir, "firmware.factory.bin")
    if os.path.isfile(factory):
        files.append(("factory", "firmware.factory.bin", read(factory), 0))
    parts = []
    for role, name, data, offset in files:
        with open(os.path.join(board_dir, name), "wb") as f:
            f.write(data)
        entry = part_entry(role, board + "/" + name, data, offset)
        if role == "app":
            entry["tag"] = "WHIPFW:%s:%s:%s;" % (board, tag_ver, api)
        parts.append(entry)
    print("release: %-8s %-28s %s %s/%s %8d B" % (env, board, head["chip"], head["mode"],
                                                  head["freq"], len(fw)), flush=True)
    return board, int(api), {
        "env": env,
        "family": "esp",
        "chip": head["chip"],
        "flash": {"mode": head["mode"], "freq": head["freq"], "size": head["size"]},
        "layout": {k: v for k, v in lay.items() if k != "app"},
        "parts": parts,
    }


def git_rev():
    try:
        rev = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                                      stderr=subprocess.DEVNULL).decode().strip()
        dirty = subprocess.call(["git", "diff", "--quiet", "HEAD"], cwd=ROOT,
                                stderr=subprocess.DEVNULL) != 0
        return rev + ("-dirty" if dirty else "")
    except (OSError, subprocess.CalledProcessError):
        return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--no-build", action="store_true", help="package the existing builds")
    ap.add_argument("-e", "--env", action="append", help="only this env (repeatable)")
    args = ap.parse_args()

    version = fw_version()
    envs = release_envs()
    if args.env:
        unknown = [e for e in args.env if e not in envs]
        if unknown:
            fail("not release envs: " + ", ".join(unknown))
        envs = args.env
    if not envs:
        fail("no [env:*] has custom_release = yes")
    if not args.no_build:
        build(envs)

    out_dir = os.path.join(ROOT, "dist", "whip-" + version)
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)
    boards = {}
    apis = set()
    for env in envs:
        board, api, entry = package_env(env, version, out_dir)
        if board in boards:
            fail("%s and %s both build board %s" % (boards[board]["env"], env, board))
        boards[board] = entry
        apis.add(api)
    if len(apis) != 1:
        fail("images disagree on api: %s" % sorted(apis))

    manifest = {
        "schema": 1,
        "version": version,
        "api": apis.pop(),
        "git": git_rev(),
        "built": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "boards": boards,
    }
    with open(os.path.join(out_dir, "manifest.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    zip_path = out_dir + ".zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for base, _, names in os.walk(out_dir):
            for name in sorted(names):
                full = os.path.join(base, name)
                z.write(full, os.path.relpath(full, os.path.dirname(out_dir)))
    print("release: v%s, %d boards -> %s" % (version, len(boards), os.path.relpath(zip_path, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
