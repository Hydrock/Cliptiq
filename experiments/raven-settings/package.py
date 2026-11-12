"""Stage only experiment executables, instructions and required notices."""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parents[2]
RAVEN = ROOT / ".toolchains/raven"
DEST = ROOT / "dist/raven-settings"
COMMON = {
    "THIRD_PARTY_NOTICES.md": ROOT / "THIRD_PARTY_NOTICES.md",
    "libgrapheme-LICENSE": ROOT / "third_party/libgrapheme/LICENSE",
    "TinyXML2-LICENSE": ROOT / "third_party/tinyxml2/LICENSE.txt",
}
RAVEN_LICENSES = {
    "Raven-LICENSE": RAVEN / "LICENSE",
    "Raven-NOTICE": RAVEN / "NOTICE",
    "Raven-THIRD_PARTY.md": RAVEN / "THIRD_PARTY.md",
    "stb-LICENSE": RAVEN / "third_party/stb/LICENSE",
    "nanosvg-LICENSE": RAVEN / "third_party/nanosvg/LICENSE.txt",
    "Roboto-LICENSE": RAVEN / "third_party/roboto/LICENSE",
    "RobotoMono-LICENSE": RAVEN / "third_party/roboto-mono/LICENSE",
}


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    result = {}
    for variant in ("native", "raven"):
        folder = DEST / variant
        folder.mkdir(exist_ok=True)
        exe = ROOT / f"out/raven-settings/settings-bin/settings_{variant}.exe"
        files = {exe.name: exe, "README.md": Path(__file__).with_name("README.md")}
        files |= {f"licenses/{k}": v for k, v in (COMMON | (RAVEN_LICENSES if variant == "raven" else {})).items()}
        for name, source in files.items():
            target = folder / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        # Inventory excludes mutable user-created profiles and the manifest itself.
        inventory = [{"path": name, "bytes": (folder / name).stat().st_size,
                      "sha256": hashlib.sha256((folder / name).read_bytes()).hexdigest()}
                     for name in sorted(files)]
        manifest = folder / "manifest.json"
        manifest.write_text(json.dumps(dict(variant=variant, raven_commit="9f0cfa6fe3da33022a577e06a6ea843a5ef3f76e" if variant == "raven" else None,
                                            files=inventory), indent=2) + "\n")
        archive = DEST / f"cliptiq-settings-{variant}-windows-x64.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
            for name in sorted([*files, "manifest.json"]):
                z.write(folder / name, f"{variant}/{name}")
        result[variant] = dict(exe_bytes=exe.stat().st_size,
                               package_bytes=sum(f["bytes"] for f in inventory) + manifest.stat().st_size,
                               zip_bytes=archive.stat().st_size,
                               zip_sha256=hashlib.sha256(archive.read_bytes()).hexdigest())
    (DEST / "sizes.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
