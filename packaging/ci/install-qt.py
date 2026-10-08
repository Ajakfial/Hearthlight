#!/usr/bin/env python3
"""Minimal Qt installer for Hearthlight CI.

Why this exists instead of aqtinstall: aqt 3.3.0 (the latest release)
predates Qt's restructured online repository (nested per-version metadata
dirs like qt6_6103/qt6_6103, renamed host trees like windows_x86, renamed
arch flavors like win64_msvc2022_64) and fails with "packages not found
while parsing XML". This script implements the small subset CI needs and
discovers everything from the repository metadata itself:

  1. Fetch Updates.xml (nested layout first, flat as fallback).
  2. Take the base arch package (the entry bundling qtbase-*.7z).
     Since Qt 6.10 the base archives already include qtsvg; if not, the
     matching addons.qtsvg package is added automatically.
  3. Download its 7z archives (py7zr) and extract them into
     <outputdir>/<version>/<arch>/, the same layout aqt produces.

Only stdlib + py7zr (pip install py7zr) are required.

Example:
  python3 packaging/ci/install-qt.py --host linux --version 6.10.3 \\
      --arch gcc_64 --outputdir Qt
"""

import argparse
import hashlib
import os
import re
import shutil
import sys
import tempfile
import urllib.request
import xml.etree.ElementTree as ET

BASES = [
    "https://download.qt.io",
    "https://mirrors.aliyun.com/qt",
]

OS_DIRS = {
    "linux": "linux_x64",
    "mac": "mac_x64",
    "windows": "windows_x86",
}

UA = {"User-Agent": "Hearthlight-CI-Qt-Installer/1.0"}


def log(msg):
    print(msg, flush=True)


def fetch(url, dest):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=120) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f, length=1024 * 256)


def try_fetch(url, dest):
    try:
        fetch(url, dest)
        return True
    except Exception as e:
        log(f"  skip {url} ({e.__class__.__name__})")
        return False


def split_archives(text):
    return [a.strip() for a in re.split(r"[,\s]+", text or "") if a.strip()]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True, choices=sorted(OS_DIRS))
    ap.add_argument("--version", required=True, help="e.g. 6.10.3")
    ap.add_argument("--arch", required=True,
                    help="repository flavor, e.g. linux_gcc_64, clang_64, win64_msvc2022_64")
    ap.add_argument("--outputdir", required=True)
    ap.add_argument("--base", action="append", default=[],
                    help="extra mirror base URL, tried in order after defaults")
    args = ap.parse_args()

    try:
        import py7zr  # noqa: F401
    except ImportError:
        sys.exit("py7zr is required: python3 -m pip install py7zr")

    vdot = args.version.replace(".", "")
    osdir = OS_DIRS[args.host]
    bases = list(dict.fromkeys(BASES + args.base))
    repodir = None
    xml_text = None
    for base in bases:
        for nested in (True, False):
            folder = f"qt6_{vdot}/qt6_{vdot}" if nested else f"qt6_{vdot}"
            url = f"{base}/online/qtsdkrepository/{osdir}/desktop/{folder}/Updates.xml"
            tmp = os.path.join(tempfile.gettempdir(), "hearthlight-qt-updates.xml")
            log(f"Trying {url}")
            if not try_fetch(url, tmp):
                continue
            try:
                with open(tmp, "r", encoding="utf-8", errors="replace") as f:
                    xml_text = f.read()
                ET.fromstring(xml_text)  # validate
                repodir = url.rsplit("/", 1)[0]
                break
            except Exception as e:
                log(f"  unparsable ({e}); continuing")
                continue
        if xml_text:
            break
    if not xml_text:
        sys.exit("Could not fetch usable Updates.xml from any mirror")

    root = ET.fromstring(xml_text)
    # Path of the metadata dir relative to the mirror root, reused so every
    # mirror serves the identical layout (e.g. online/qtsdkrepository/…).
    repo_suffix = None
    for base in bases:
        if repodir.startswith(base):
            repo_suffix = repodir[len(base):].lstrip("/")
            break
    assert repo_suffix, "mirror bookkeeping failed"

    def entry_version(pu):
        return (pu.findtext("Version") or "").strip()

    packages = []  # (name, version, [archives], targetdir)
    for pu in root.findall("PackageUpdate"):
        name = (pu.findtext("Name") or "").strip()
        archives = split_archives(pu.findtext("DownloadableArchives"))
        target = ""
        ops = pu.find("Operations")
        if ops is not None:
            for op in ops.findall("Operation"):
                op_args = [a.text.strip() for a in op.findall("Argument") if a.text]
                hit = next((a for a in op_args if a.startswith("@TargetDir@/")), "")
                if hit:
                    target = hit[len("@TargetDir@/"):].strip("/")
                    break
        if name and archives:
            packages.append((name, entry_version(pu), archives, target))

    # Base package: exactly qt.qt6.<vdot>.<flavor> bundling qtbase archives
    # (one Updates.xml covers every arch of that host/version).
    want = f"qt.qt6.{vdot}.{args.arch}"
    base_hits = [(n, v, a, t) for n, v, a, t in packages
                 if n == want and any(x.startswith("qtbase-") for x in a)]
    if len(base_hits) != 1:
        cands = sorted(n for n, v, a, t in packages
                       if re.fullmatch(rf"qt\.qt6\.{vdot}\.[A-Za-z0-9_]+", n)
                       and any(x.startswith("qtbase-") for x in a))
        sys.exit(f"Base package {want} not found; candidates were {cands}")
    base_name, base_version, base_archives, base_target = base_hits[0]
    if not base_target:
        sys.exit(f"Base package {want} has no install target in its metadata")
    flavor = args.arch
    log(f"Base package: {base_name} ({len(base_archives)} archives -> {base_target})")

    # IFW layout: archives live in a per-package dir, prefixed with the
    # package Version (e.g. 6.10.3-0-202603310407qtbase-….7z).
    wanted = [(base_name, base_version, a) for a in base_archives]
    if not any(a.startswith("qtsvg-") for a in base_archives):
        svg = [(n, v, a) for n, v, a, t in packages
               if n == f"qt.qt6.{vdot}.addons.qtsvg.{flavor}" and a]
        if not svg:
            sys.exit("qtsvg is neither bundled nor a separate package; check the repo layout")
        log(f"Adding separate module package: {svg[0][0]}")
        wanted += [(svg[0][0], svg[0][1], a) for a in svg[0][2]]
        log(f"Adding separate module package: {svg[0][0]}")
        archives += svg[0][1]

    outdir = os.path.abspath(args.outputdir)
    moc_name = "moc.exe" if args.host == "windows" else "moc"

    def find_bindir():
        verdir = os.path.join(outdir, args.version)
        if not os.path.isdir(verdir):
            return None
        for sub in sorted(os.listdir(verdir)):
            cand = os.path.join(verdir, sub, "bin", moc_name)
            if os.path.isfile(cand):
                return os.path.dirname(cand)
        return None

    bindir = find_bindir()
    if bindir:
        log(f"Already installed under {os.path.dirname(bindir)}; skipping download")
    else:
        tmpdir = tempfile.mkdtemp(prefix="hearthlight-qt-dl-")
        try:
            import py7zr
            total = len(wanted)
            for i, (pkg, ver, archive) in enumerate(wanted):
                # Per-package dir, Version-prefixed filename.
                remote = f"{ver}{archive}"
                log(f"[{i + 1}/{total}] {pkg}/{remote}")
                local = os.path.join(tmpdir, archive)
                ok = False
                for base in bases:
                    url = f"{base}/{repo_suffix}/{pkg}/{remote}"
                    try:
                        fetch(url, local)
                        ok = True
                        break
                    except Exception as e:
                        log(f"  mirror failed ({e.__class__.__name__}); trying next")
                if not ok:
                    sys.exit(f"Could not download {pkg}/{remote} from any mirror")
                dest = os.path.join(outdir, base_target)
                os.makedirs(dest, exist_ok=True)
                with py7zr.SevenZipFile(local, "r") as z:
                    z.extractall(dest)
        finally:
            shutil.rmtree(tmpdir, ignore_errors=True)

    # Discover the real install dir (internal arch dir may differ in case).
    bindir = find_bindir()
    if not bindir:
        sys.exit(f"Install finished but no moc found under {os.path.join(outdir, args.version)}")
    prefix = os.path.dirname(bindir)
    cmake_dir = os.path.join(prefix, "lib", "cmake", "Qt6")
    if not os.path.isdir(cmake_dir):
        sys.exit(f"Qt6 CMake config missing at {cmake_dir}")

    # integrity: hash a sentinel binary so logs prove what ran
    h = hashlib.sha256()
    with open(os.path.join(bindir, moc_name), "rb") as f:
        h.update(f.read(1 << 20))
    log(f"Qt ready: {prefix} (moc sha256:{h.hexdigest()[:16]}…)")

    exports = [f"HEARTHLIGHT_QT_BIN={bindir}",
               f"HEARTHLIGHT_QT_PREFIX={prefix}",
               f"HEARTHLIGHT_QT_CMAKE={cmake_dir}"]
    gh_env = os.environ.get("GITHUB_ENV")
    gh_path = os.environ.get("GITHUB_PATH")
    if gh_env and gh_path:
        with open(gh_env, "a", encoding="utf-8") as f:
            f.write(f"Qt6_DIR={cmake_dir}\n")
            f.write(f"CMAKE_PREFIX_PATH={prefix}\n")
        with open(gh_path, "a", encoding="utf-8") as f:
            f.write(f"{bindir}\n")
        log("Exported Qt6_DIR, CMAKE_PREFIX_PATH and PATH to the workflow")
    else:
        for line in exports:
            print(line)


if __name__ == "__main__":
    main()
