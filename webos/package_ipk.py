#!/usr/bin/env python3
"""
Lightweight packaging script for LG webOS TV applications (.ipk).
Generates standard Debian/opkg-compatible AR archives without requiring Node.js or ares-package.
"""
import os
import sys
import json
import time
import io
import tarfile
import struct

def build_ar_header(filename: str, size: int, mtime: int = 0, mode: int = 0o100644, uid: int = 0, gid: int = 0) -> bytes:
    # 60 bytes header
    # 0..16: filename (padded with space)
    # 16..28: mtime (decimal, padded with space)
    # 28..34: uid
    # 34..40: gid
    # 40..48: mode (octal, padded with space)
    # 48..58: size (decimal, padded with space)
    # 58..60: `\n
    name_field = f"{filename:<16}"[:16].encode('ascii')
    mtime_field = f"{mtime:<12}"[:12].encode('ascii')
    uid_field = f"{uid:<6}"[:6].encode('ascii')
    gid_field = f"{gid:<6}"[:6].encode('ascii')
    mode_field = f"{oct(mode)[2:]:<8}"[:8].encode('ascii')
    size_field = f"{size:<10}"[:10].encode('ascii')
    magic_field = b"`\n"
    return name_field + mtime_field + uid_field + gid_field + mode_field + size_field + magic_field

def create_tar_gz(files_dict: dict) -> bytes:
    """files_dict: {archive_path: (file_bytes, mode)}"""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tar:
        for arcname, (data, mode) in files_dict.items():
            ti = tarfile.TarInfo(name=arcname)
            ti.size = len(data)
            ti.mtime = int(time.time())
            ti.mode = mode
            ti.type = tarfile.REGTYPE
            tar.addfile(ti, io.BytesIO(data))
    return buf.getvalue()

def package_webos_app(app_dir: str, out_dir: str) -> str:
    appinfo_path = os.path.join(app_dir, "appinfo.json")
    if not os.path.exists(appinfo_path):
        raise FileNotFoundError(f"Missing appinfo.json in {app_dir}")

    with open(appinfo_path, "r", encoding="utf-8") as f:
        appinfo = json.load(f)

    app_id = appinfo.get("id", "org.nxtorrent.player")
    version = appinfo.get("version", "1.0.0")
    title = appinfo.get("title", "NX Torrent Player")

    print(f"Packaging webOS application: {app_id} v{version} ({title})")

    # 1. debian-binary
    debian_binary = b"2.0\n"

    # 2. Collect data files
    install_prefix = f"usr/palm/applications/{app_id}"
    data_files = {}
    total_size = 0

    # Include specific app files and subdirectories
    ignore_items = {"package.json", "package-lock.json", "node_modules", "dist", ".git", "package_ipk.py", "README.md"}
    for root, dirs, files in os.walk(app_dir):
        # Exclude ignored dirs
        dirs[:] = [d for d in dirs if d not in ignore_items and not d.startswith(".")]
        for file in files:
            if file in ignore_items or file.endswith(".ipk"):
                continue
            full_path = os.path.join(root, file)
            rel_path = os.path.relpath(full_path, app_dir).replace("\\", "/")
            arcname = f"{install_prefix}/{rel_path}"
            with open(full_path, "rb") as f:
                content = f.read()
            data_files[arcname] = (content, 0o644)
            total_size += len(content)

    data_tar_gz = create_tar_gz(data_files)

    # 3. control.tar.gz
    installed_size_kb = (total_size + 1023) // 1024
    control_content = (
        f"Package: {app_id}\n"
        f"Version: {version}\n"
        f"Section: misc\n"
        f"Priority: optional\n"
        f"Architecture: all\n"
        f"Maintainer: Langegen\n"
        f"Description: {title}\n"
        f"Installed-Size: {installed_size_kb}\n"
    ).encode("utf-8")

    control_tar_gz = create_tar_gz({
        "./control": (control_content, 0o644)
    })

    # 4. Assemble AR archive (.ipk)
    ipk_filename = f"{app_id}_{version}_all.ipk"
    os.makedirs(out_dir, exist_ok=True)
    ipk_path = os.path.join(out_dir, ipk_filename)

    with open(ipk_path, "wb") as f:
        # AR magic
        f.write(b"!<arch>\n")

        # Member 1: debian-binary
        f.write(build_ar_header("debian-binary", len(debian_binary)))
        f.write(debian_binary)
        if len(debian_binary) % 2 != 0:
            f.write(b"\n")

        # Member 2: control.tar.gz
        f.write(build_ar_header("control.tar.gz", len(control_tar_gz)))
        f.write(control_tar_gz)
        if len(control_tar_gz) % 2 != 0:
            f.write(b"\n")

        # Member 3: data.tar.gz
        f.write(build_ar_header("data.tar.gz", len(data_tar_gz)))
        f.write(data_tar_gz)
        if len(data_tar_gz) % 2 != 0:
            f.write(b"\n")

    print(f"Successfully created: {ipk_path} ({os.path.getsize(ipk_path)} bytes)")
    return ipk_path

if __name__ == "__main__":
    script_dir = os.path.dirname(os.path.abspath(__file__))
    dist_dir = os.path.join(script_dir, "dist")
    package_webos_app(script_dir, dist_dir)
