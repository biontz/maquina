#!/usr/bin/env python3
"""
make_sysroot.py DIR - minimal Debian 12 (bookworm) arm64 sysroot for
cross-compiling the plugins for Raspberry Pi OS (64-bit), without root.

Downloads the C/C++ runtime and development packages from deb.debian.org,
checks each .deb against the SHA-256 in the (HTTPS-fetched) Packages index,
and unpacks them into DIR. bookworm's glibc 2.36 / libstdc++ 12 match
Raspberry Pi OS bookworm, so binaries also run on newer releases.

SPDX-License-Identifier: GPL-3.0-only
"""
import hashlib
import io
import lzma
import os
import subprocess
import sys
import tarfile
import tempfile
import urllib.request

MIRROR = "https://deb.debian.org/debian"
DIST = "bookworm"
PACKAGES = ["libc6", "libc6-dev", "linux-libc-dev", "libgcc-s1", "libgcc-12-dev",
            "libstdc++6", "libstdc++-12-dev", "libasan8", "libatomic1", "libgomp1",
            "libitm1", "liblsan0", "libtsan2", "libubsan1", "libhwasan0",
            # test host only (tools/lv2_render under qemu): lilv and friends
            "lv2-dev", "liblilv-0-0", "liblilv-dev", "libserd-0-0", "libserd-dev",
            "libsord-0-0", "libsord-dev", "libsratom-0-0", "libsratom-dev"]


def relative_links(member, dest):
    """tarfile 'data' filter, after turning absolute symlinks into relative
    ones (Debian ships e.g. libfoo.so -> /lib/aarch64-linux-gnu/libfoo.so.1)."""
    if member.issym() and member.linkname.startswith("/"):
        here = os.path.dirname("/" + member.name.lstrip("./"))
        member = member.replace(linkname=os.path.relpath(member.linkname, here), deep=False)
    return tarfile.data_filter(member, dest)


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as r:
        return r.read()


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    root = os.path.abspath(sys.argv[1])
    os.makedirs(root, exist_ok=True)
    index = lzma.decompress(fetch(f"{MIRROR}/dists/{DIST}/main/binary-arm64/Packages.xz")).decode()
    pkgs = {}
    for stanza in index.split("\n\n"):
        fields = dict(l.split(": ", 1) for l in stanza.splitlines() if ": " in l and not l.startswith(" "))
        if fields.get("Package") in PACKAGES:
            pkgs[fields["Package"]] = fields
    missing = set(PACKAGES) - set(pkgs)
    if missing:
        sys.exit(f"not in index: {sorted(missing)}")
    for name in PACKAGES:
        f = pkgs[name]
        data = fetch(f"{MIRROR}/{f['Filename']}")
        if hashlib.sha256(data).hexdigest() != f["SHA256"]:
            sys.exit(f"{name}: SHA-256 mismatch")
        with tempfile.TemporaryDirectory() as tmp:
            deb = os.path.join(tmp, "p.deb")
            with open(deb, "wb") as fh:
                fh.write(data)
            subprocess.check_call(["ar", "x", deb], cwd=tmp)
            member = next(m for m in os.listdir(tmp) if m.startswith("data.tar"))
            with tarfile.open(os.path.join(tmp, member)) as t:
                t.extractall(root, filter=relative_links)
        print(f"  {name} {f['Version']}")
    print(f"sysroot ready: {root}")


if __name__ == "__main__":
    main()
