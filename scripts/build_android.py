 #!/usr/bin/env python3
"""Build an installable Android APK for the Insland demo.

The script uses the Android SDK build-tools (aapt2, zipalign, apksigner) and the
NDK CMake toolchain directly, so it does not need Gradle.  It finds the SDK,
NDK, JDK and Ninja automatically and downloads the NDK on first use.

Examples:
    py scripts/build_android.py
    py scripts/build_android.py --abi x86_64
    py scripts/build_android.py --ndk "C:/Android/Sdk/ndk/27.3.13750724"
    py scripts/build_android.py --output dist/insland.apk

The resulting APK is debug-signed with the standard ~/.android/debug.keystore
(created on demand).  Install it with:

    adb install -r build-android/insland-arm64-v8a.apk
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# NDK r27c (27.3.13750724) is the current stable release used by the engine.
NDK_VERSION = "27.3.13750724"
NDK_ZIPS = {
    "win32": "android-ndk-r27c-windows.zip",
    "darwin": "android-ndk-r27c-darwin.zip",
    "linux": "android-ndk-r27c-linux.zip",
}
NDK_BASE_URL = "https://dl.google.com/android/repository/"

MIN_SDK = 24
TARGET_SDK = 37
PACKAGE = "com.insland.engine"

DEFAULT_ASSETS = ["model.glb", "draco_box.glb", "demo.scene.json"]


def log(message: str) -> None:
    print(f"[android] {message}", flush=True)


def fail(message: str) -> "NoReturn":
    print(f"[android] error: {message}", file=sys.stderr, flush=True)
    raise SystemExit(1)


def first_existing(candidates: list[Path | None]) -> Path | None:
    for candidate in candidates:
        if candidate and candidate.exists():
            return candidate
    return None


def find_sdk(explicit: str | None) -> Path:
    local_appdata = os.environ.get("LOCALAPPDATA")
    candidates = [
        Path(explicit) if explicit else None,
        Path(os.environ["ANDROID_HOME"]) if os.environ.get("ANDROID_HOME") else None,
        Path(os.environ["ANDROID_SDK_ROOT"]) if os.environ.get("ANDROID_SDK_ROOT") else None,
        Path(local_appdata) / "Android" / "Sdk" if local_appdata else None,
        Path.home() / "Android" / "Sdk",
        Path.home() / "Library" / "Android" / "sdk",
        Path("/opt/android-sdk"),
    ]
    sdk = first_existing(candidates)
    if not sdk:
        fail("Android SDK not found. Install Android Studio or pass --sdk PATH.")
    return sdk


def version_key(path: Path) -> tuple[int, ...]:
    parts = []
    for piece in path.name.replace("-", ".").split("."):
        try:
            parts.append(int(piece))
        except ValueError:
            parts.append(0)
    return tuple(parts)


def find_platform(sdk: Path) -> Path:
    platforms = sdk / "platforms"
    if not platforms.is_dir():
        fail(f"no platforms directory in {sdk}")
    candidates = sorted(
        (p for p in platforms.iterdir() if (p / "android.jar").is_file()),
        key=version_key,
    )
    if not candidates:
        fail(f"no Android platform with android.jar in {platforms}")
    return candidates[-1]


def find_build_tools(sdk: Path) -> Path:
    tools = sdk / "build-tools"
    if not tools.is_dir():
        fail(f"no build-tools directory in {sdk}")
    candidates = sorted(
        (p for p in tools.iterdir() if (p / ("aapt2.exe" if os.name == "nt" else "aapt2")).exists()),
        key=version_key,
    )
    if not candidates:
        fail(f"no usable build-tools in {tools}")
    return candidates[-1]


def find_ndk(sdk: Path, explicit: str | None) -> Path | None:
    candidates = [
        Path(explicit) if explicit else None,
        Path(os.environ["ANDROID_NDK_HOME"]) if os.environ.get("ANDROID_NDK_HOME") else None,
        Path(os.environ["ANDROID_NDK_ROOT"]) if os.environ.get("ANDROID_NDK_ROOT") else None,
    ]
    found = first_existing(candidates)
    if found:
        return found

    ndk_dir = sdk / "ndk"
    if ndk_dir.is_dir():
        versions = sorted(
            (p for p in ndk_dir.iterdir() if (p / "build" / "cmake" / "android.toolchain.cmake").is_file()),
            key=version_key,
        )
        if versions:
            return versions[-1]
    return None


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    log(f"downloading {url}")
    with urllib.request.urlopen(url) as response, destination.open("wb") as output:
        total = int(response.headers.get("Content-Length", "0"))
        received = 0
        while True:
            chunk = response.read(1024 * 1024)
            if not chunk:
                break
            output.write(chunk)
            received += len(chunk)
            if total > 0:
                percent = received * 100 // total
                if percent % 5 == 0:
                    print(f"\r[android]   {percent:3d}%  {received // (1024 * 1024)} MiB", end="", flush=True)
    print(flush=True)


def install_ndk(sdk: Path) -> Path:
    host = "win32" if os.name == "nt" else ("darwin" if sys.platform == "darwin" else "linux")
    archive_name = NDK_ZIPS[host]
    cache = ROOT / "build-android" / "downloads" / archive_name
    if not cache.is_file():
        download(NDK_BASE_URL + archive_name, cache)

    ndk_dir = sdk / "ndk"
    ndk_dir.mkdir(parents=True, exist_ok=True)
    target = ndk_dir / NDK_VERSION
    if target.is_dir():
        return target

    log(f"extracting {archive_name} to {ndk_dir}")
    with zipfile.ZipFile(cache) as archive:
        root_name = archive.namelist()[0].split("/")[0]
        archive.extractall(ndk_dir)
    extracted = ndk_dir / root_name
    if extracted != target:
        if target.exists():
            shutil.rmtree(target)
        extracted.rename(target)

    if not (target / "build" / "cmake" / "android.toolchain.cmake").is_file():
        fail(f"the NDK archive did not contain the expected toolchain: {target}")
    return target


def find_cmake(explicit: str | None) -> Path:
    candidates = [
        Path(explicit) if explicit else None,
        Path(os.environ["CMAKE"]) if os.environ.get("CMAKE") else None,
        Path(shutil.which("cmake")) if shutil.which("cmake") else None,
        Path("C:/Program Files/CMake/bin/cmake.exe"),
        Path("/usr/bin/cmake"),
        Path("/usr/local/bin/cmake"),
    ]
    found = first_existing(candidates)
    if not found:
        fail("CMake not found. Install CMake or pass --cmake PATH.")
    return found


def find_ninja(explicit: str | None, sdk: Path) -> Path:
    candidates = [
        Path(explicit) if explicit else None,
        Path(shutil.which("ninja")) if shutil.which("ninja") else None,
        Path("C:/Program Files/CMake/bin/ninja.exe"),
        Path("C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"),
        Path("C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"),
    ]
    found = first_existing(candidates)
    if found:
        return found

    sdk_cmake = sdk / "cmake"
    if sdk_cmake.is_dir():
        for directory in sorted(sdk_cmake.iterdir(), key=version_key):
            candidate = directory / "bin" / ("ninja.exe" if os.name == "nt" else "ninja")
            if candidate.is_file():
                return candidate

    fail("Ninja not found. Install Ninja or pass --ninja PATH.")
    return Path()  # unreachable


def find_jdk(explicit: str | None) -> Path | None:
    candidates = [
        Path(explicit) if explicit else None,
        Path(os.environ["JAVA_HOME"]) if os.environ.get("JAVA_HOME") else None,
        Path("C:/Program Files/Android/Android Studio/jbr"),
        Path("/Applications/Android Studio.app/Contents/jbr/Contents/Home"),
        Path("/opt/android-studio/jbr"),
    ]
    return first_existing(candidates)


def run(command: list[str], cwd: Path | None = None) -> None:
    log("$ " + " ".join(str(part) for part in command))
    subprocess.run([str(part) for part in command], cwd=cwd, check=True)


def copy_assets(source: Path, destination: Path, copy_all: bool) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    if not source.is_dir():
        log(f"asset directory not found, packaging no assets: {source}")
        return

    if copy_all:
        for entry in source.iterdir():
            if entry.is_file():
                shutil.copy2(entry, destination / entry.name)
            elif entry.is_dir():
                shutil.copytree(entry, destination / entry.name, dirs_exist_ok=True)
        return

    for name in DEFAULT_ASSETS:
        file = source / name
        if file.is_file():
            shutil.copy2(file, destination / name)

    textures = source / "textures"
    if textures.is_dir():
        shutil.copytree(textures, destination / "textures", dirs_exist_ok=True)


def ensure_debug_keystore(jdk: Path | None) -> Path:
    keystore = Path.home() / ".android" / "debug.keystore"
    if keystore.is_file():
        return keystore
    if not jdk:
        fail("no JDK found to create the debug keystore. Pass --jdk PATH.")
    keytool = jdk / "bin" / ("keytool.exe" if os.name == "nt" else "keytool")
    if not keytool.is_file():
        fail(f"keytool not found in {jdk}")
    keystore.parent.mkdir(parents=True, exist_ok=True)
    run([
        str(keytool),
        "-genkeypair",
        "-v",
        "-keystore", str(keystore),
        "-storepass", "android",
        "-alias", "androiddebugkey",
        "-keypass", "android",
        "-dname", "CN=Android Debug,O=Android,C=US",
        "-keyalg", "RSA",
        "-keysize", "2048",
        "-validity", "10000",
    ])
    return keystore


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the Insland Android APK.")
    parser.add_argument("--abi", default="arm64-v8a", choices=["arm64-v8a", "x86_64", "x86", "armeabi-v7a"])
    parser.add_argument("--release", action="store_true", help="build with CMAKE_BUILD_TYPE=Release (default)")
    parser.add_argument("--debug", action="store_true", help="build with CMAKE_BUILD_TYPE=Debug")
    parser.add_argument("--sdk", help="Android SDK directory")
    parser.add_argument("--ndk", help="Android NDK directory")
    parser.add_argument("--cmake", help="cmake executable")
    parser.add_argument("--ninja", help="ninja executable")
    parser.add_argument("--jdk", help="JDK directory (for keytool)")
    parser.add_argument("--build-dir", default="build-android", help="CMake build directory")
    parser.add_argument("--output", help="output APK path")
    parser.add_argument("--assets", default="workspace/game/assets", help="asset directory to package")
    parser.add_argument("--all-assets", action="store_true", help="package every file in the asset directory")
    parser.add_argument("--clean", action="store_true", help="delete the build directory first")
    parser.add_argument("--no-download", action="store_true", help="never download the NDK")
    args = parser.parse_args()

    sdk = find_sdk(args.sdk)
    platform = find_platform(sdk)
    build_tools = find_build_tools(sdk)
    ndk = find_ndk(sdk, args.ndk)
    if not ndk:
        if args.no_download:
            fail("NDK not found and --no-download was given.")
        ndk = install_ndk(sdk)

    cmake = find_cmake(args.cmake)
    ninja = find_ninja(args.ninja, sdk)
    jdk = find_jdk(args.jdk)
    if jdk:
        # apksigner.bat shells out to java, which needs JAVA_HOME (or PATH).
        os.environ["JAVA_HOME"] = str(jdk)
        os.environ["PATH"] = str(jdk / "bin") + os.pathsep + os.environ.get("PATH", "")
    build_type = "Debug" if args.debug else "Release"

    build_dir = (ROOT / args.build_dir).resolve()
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    log(f"SDK         {sdk}")
    log(f"platform    {platform.name}")
    log(f"build-tools {build_tools.name}")
    log(f"NDK         {ndk}")
    log(f"CMake       {cmake}")
    log(f"Ninja       {ninja}")
    log(f"build type  {build_type}")
    log(f"ABI         {args.abi}")

    run([
        str(cmake),
        "-S", str(ROOT),
        "-B", str(build_dir),
        "-G", "Ninja",
        f"-DCMAKE_MAKE_PROGRAM={ninja}",
        f"-DCMAKE_TOOLCHAIN_FILE={ndk / 'build' / 'cmake' / 'android.toolchain.cmake'}",
        f"-DANDROID_ABI={args.abi}",
        f"-DANDROID_PLATFORM=android-{MIN_SDK}",
        "-DANDROID_STL=c++_static",
        f"-DCMAKE_BUILD_TYPE={build_type}",
        "-DINSLAND_BUILD_PROJECTS=OFF",
    ])

    run([str(cmake), "--build", str(build_dir), "--target", "InslandGame"])

    libraries = sorted(build_dir.rglob("libinsland.so"))
    if not libraries:
        fail(f"libinsland.so was not produced under {build_dir}")
    library = libraries[0]
    log(f"native library {library} ({library.stat().st_size // 1024} KiB)")

    package_dir = build_dir / "apk"
    if package_dir.exists():
        shutil.rmtree(package_dir)
    package_dir.mkdir(parents=True)

    manifest = ROOT / "android" / "AndroidManifest.xml"
    if not manifest.is_file():
        fail(f"manifest not found: {manifest}")

    assets_dir = package_dir / "assets"
    copy_assets((ROOT / args.assets).resolve(), assets_dir, args.all_assets)

    aapt2 = build_tools / ("aapt2.exe" if os.name == "nt" else "aapt2")
    zipalign = build_tools / ("zipalign.exe" if os.name == "nt" else "zipalign")
    apksigner = build_tools / ("apksigner.bat" if os.name == "nt" else "apksigner")

    unsigned = package_dir / "unsigned.apk"
    aligned = package_dir / "aligned.apk"
    output = Path(args.output).resolve() if args.output else build_dir / f"insland-{args.abi}.apk"

    run([
        str(aapt2), "link",
        "-o", str(unsigned),
        "-I", str(platform / "android.jar"),
        "--manifest", str(manifest),
        "--min-sdk-version", str(MIN_SDK),
        "--target-sdk-version", str(TARGET_SDK),
        "--version-code", "1",
        "--version-name", "0.1.0",
        "-A", str(assets_dir),
    ])

    log(f"adding lib/{args.abi}/libinsland.so")
    with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as archive:
        archive.write(library, f"lib/{args.abi}/libinsland.so")

    run([str(zipalign), "-f", "-p", "4", str(unsigned), str(aligned)])

    keystore = ensure_debug_keystore(jdk)
    output.parent.mkdir(parents=True, exist_ok=True)
    run([
        str(apksigner), "sign",
        "--ks", str(keystore),
        "--ks-pass", "pass:android",
        "--key-pass", "pass:android",
        "--out", str(output),
        str(aligned),
    ])

    run([str(apksigner), "verify", "--print-certs", str(output)])

    log(f"APK written to {output} ({output.stat().st_size // (1024 * 1024)} MiB)")
    log(f"install with: adb install -r \"{output}\"")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
