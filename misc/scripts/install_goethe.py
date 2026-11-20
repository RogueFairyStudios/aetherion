#!/usr/bin/env python3
"""Download and stage pre-built Goethe Dialog System native SDK binaries from GitHub Releases."""

if __name__ != "__main__":
    raise SystemExit(f'Utility script "{__file__}" should not be used as a module!')

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.error
import urllib.request
import zipfile

DEFAULT_VERSION = "0.2.0"
DEFAULT_REPO = "RogueFairyStudios/goethe"


def default_deps_folder() -> str:
    local_app_data = os.getenv("LOCALAPPDATA")
    if local_app_data and not os.getenv("MSYSTEM"):
        return os.path.join(local_app_data, "Godot", "build_deps")
    return os.path.join("bin", "build_deps")


def detect_target_triplet(override_platform: str = "", override_arch: str = "") -> tuple[str, str, str]:
    sys_name = (override_platform or platform.system()).lower()
    machine = (override_arch or platform.machine()).lower()

    if sys_name in ("windows", "win32", "msys", "cygwin"):
        plat = "windows"
        arch = "arm64" if machine in ("arm64", "aarch64") else "x86_64"
        ext = "zip"
    elif sys_name in ("darwin", "macos", "osx"):
        plat = "macos"
        arch = "universal"
        ext = "tar.gz"
    elif sys_name in ("linux", "linuxbsd"):
        plat = "linux"
        arch = "arm64" if machine in ("arm64", "aarch64") else "x86_64"
        ext = "tar.gz"
    else:
        raise RuntimeError(f"Unsupported host platform for pre-built Goethe SDK: {sys_name}")

    return plat, arch, ext


class _StripAuthOnCrossHostRedirect(urllib.request.HTTPRedirectHandler):
    """Strip Authorization header when GitHub API redirects to Azure/S3 blob storage."""

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        new_req = super().redirect_request(req, fp, code, msg, headers, newurl)
        if new_req is not None and req.host != new_req.host:
            new_req.headers.pop("Authorization", None)
            new_req.unredirected_hdrs.pop("Authorization", None)
        return new_req


def _resolve_token() -> str:
    for env_key in ("GOETHE_GITHUB_TOKEN", "GH_TOKEN", "GITHUB_TOKEN"):
        val = os.getenv(env_key, "").strip()
        if val:
            return val
    gh_bin = shutil.which("gh")
    if gh_bin:
        try:
            out = subprocess.check_output([gh_bin, "auth", "token"], stderr=subprocess.DEVNULL, text=True).strip()
            if out:
                return out
        except Exception:
            pass
    return ""


def download_release_asset(repo: str, tag: str, asset_name: str, dest_path: str, token: str) -> bool:
    """Download a release asset supporting both private (GitHub REST API / gh CLI) and public repos."""
    opener = urllib.request.build_opener(_StripAuthOnCrossHostRedirect())

    # 1. Authenticated GitHub REST API (works for both private and public repos)
    if token:
        meta_url = f"https://api.github.com/repos/{repo}/releases/tags/{tag}"
        meta_req = urllib.request.Request(
            meta_url,
            headers={
                "User-Agent": "Aetherion-GoetheInstaller/1.0",
                "Accept": "application/vnd.github+json",
                "Authorization": f"Bearer {token}",
            },
        )
        try:
            with opener.open(meta_req, timeout=30) as resp:
                release_json = json.loads(resp.read().decode("utf-8"))
            for asset in release_json.get("assets", []):
                if asset.get("name") == asset_name:
                    asset_api_url = asset["url"]
                    dl_req = urllib.request.Request(
                        asset_api_url,
                        headers={
                            "User-Agent": "Aetherion-GoetheInstaller/1.0",
                            "Accept": "application/octet-stream",
                            "Authorization": f"Bearer {token}",
                        },
                    )
                    with opener.open(dl_req, timeout=180) as dl_resp, open(dest_path, "wb") as out:
                        shutil.copyfileobj(dl_resp, out)
                    return True
        except urllib.error.HTTPError:
            pass

    # 2. Fallback: gh CLI if available
    gh_bin = shutil.which("gh")
    if gh_bin:
        try:
            subprocess.check_call(
                [gh_bin, "release", "download", tag, "-R", repo, "-p", asset_name, "-O", dest_path, "--clobber"],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            if os.path.isfile(dest_path):
                return True
        except Exception:
            pass

    # 3. Public direct URL fallback
    direct_url = f"https://github.com/{repo}/releases/download/{tag}/{asset_name}"
    req = urllib.request.Request(direct_url, headers={"User-Agent": "Aetherion-GoetheInstaller/1.0"})
    with opener.open(req, timeout=180) as resp, open(dest_path, "wb") as out:
        shutil.copyfileobj(resp, out)
    return True


def verify_sha256_if_available(repo: str, tag: str, archive_name: str, archive_path: str, token: str) -> None:
    sums_path = archive_path + ".sha256sums"
    try:
        download_release_asset(repo, tag, "SHA256SUMS.txt", sums_path, token)
    except Exception:
        return

    expected = ""
    try:
        with open(sums_path, "r", encoding="utf-8") as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) >= 2 and parts[1].lstrip("*") == archive_name:
                    expected = parts[0].lower()
                    break
    finally:
        if os.path.isfile(sums_path):
            os.remove(sums_path)

    if not expected:
        return

    h = hashlib.sha256()
    with open(archive_path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    actual = h.hexdigest().lower()
    if actual != expected:
        raise RuntimeError(f"SHA-256 mismatch for {archive_name}: expected {expected}, got {actual}")
    print(f"Verified SHA-256 for {archive_name}: {actual}")


def extract_archive(archive_path: str, dest_dir: str) -> None:
    os.makedirs(dest_dir, exist_ok=True)
    if archive_path.endswith(".zip"):
        with zipfile.ZipFile(archive_path, "r") as zf:
            zf.extractall(dest_dir)
    elif archive_path.endswith(".tar.gz") or archive_path.endswith(".tgz"):
        with tarfile.open(archive_path, "r:gz") as tf:
            tf.extractall(dest_dir)
    else:
        shutil.unpack_archive(archive_path, dest_dir)


def main() -> int:
    parser = argparse.ArgumentParser(description="Install pre-built Goethe SDK for Aetherion SCons builds.")
    parser.add_argument("--version", default=os.getenv("GOETHE_VERSION", DEFAULT_VERSION), help="Goethe release version (e.g. 0.2.0)")
    parser.add_argument("--repo", default=os.getenv("GOETHE_REPO", DEFAULT_REPO), help="GitHub owner/repo")
    parser.add_argument("--platform", default="", help="Override platform (windows, linux, macos)")
    parser.add_argument("--arch", default="", help="Override arch (x86_64, arm64, universal)")
    parser.add_argument("--dest", default="", help="Destination folder (defaults to Godot build_deps/goethe)")
    args = parser.parse_args()

    version = args.version.lstrip("v")
    tag = f"v{version}"
    plat, arch, ext = detect_target_triplet(args.platform, args.arch)
    token = _resolve_token()

    deps_folder = default_deps_folder()
    goethe_folder = args.dest if args.dest else os.path.join(deps_folder, "goethe")
    parent_folder = os.path.dirname(os.path.abspath(goethe_folder))
    os.makedirs(parent_folder, exist_ok=True)

    archive_name = f"goethe-sdk-{version}-{plat}-{arch}.{ext}"
    archive_path = os.path.join(parent_folder, archive_name)

    if os.path.isfile(archive_path):
        os.remove(archive_path)

    print(f"Downloading Goethe SDK {archive_name} ({args.repo}@{tag}) ...")
    download_release_asset(args.repo, tag, archive_name, archive_path, token)
    verify_sha256_if_available(args.repo, tag, archive_name, archive_path, token)

    if os.path.exists(goethe_folder):
        print(f"Removing previous Goethe SDK installation in {goethe_folder} ...")
        shutil.rmtree(goethe_folder)

    print(f"Extracting {archive_name} to {goethe_folder} ...")
    extract_archive(archive_path, goethe_folder)
    os.remove(archive_path)

    with open(os.path.join(goethe_folder, "version"), "w", encoding="utf-8") as f:
        f.write(version)

    print(f"Goethe SDK {version} ({plat}-{arch}) installed successfully to {goethe_folder}.\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
