"""Fail if the public Git file list includes game assets, fonts or unexpected files."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = {"README.md", "LICENSE", "THIRD_PARTY_NOTICES.md", "CMakeLists.txt",
              ".gitignore", ".gitattributes", "requirements.txt"}
DIRECTORIES = {"src": {".cpp", ".h", ".def"}, "tools": {".py", ".json"},
               "translations": {".json"}, "third_party": {".c", ".cpp", ".h", ".txt"}}
MOD_FILES = {"assets/wilmzh-font.wzh", "assets/wilmzh-help.wzh"}
RELEASE_FILES = {
    "Wilmot-Warehouse-ZH-v0.3.0.zip", "wilmot-warehouse-zh-source-v0.3.0.zip",
    "Wilmot-Warehouse-ZH-v0.4.0.zip", "Wilmot-Warehouse-ZH-v0.4.0-legacy-update.zip",
    "wilmot-warehouse-zh-source-v0.4.0.zip",
}


def check(paths):
    if not paths or not MOD_FILES.issubset(paths):
        raise ValueError("The staged public tree is incomplete")
    bad = []
    for name in paths:
        p = Path(name)
        if name in ROOT_FILES or name in MOD_FILES or name in RELEASE_FILES:
            continue
        if p.parts and p.parts[0] in DIRECTORIES and p.suffix in DIRECTORIES[p.parts[0]]:
            continue
        bad.append(name)
    if bad:
        raise ValueError("Unexpected public files: " + ", ".join(bad))
    print(f"PASS: {len(paths)} allowed Mod/source/license files; no game resources or fonts")


if __name__ == "__main__":
    # Use tracked paths, including staged initial commits; audit the actual upload set.
    result = subprocess.run(["git", "ls-files", "-z"], cwd=ROOT,
                            capture_output=True, check=True)
    check([n for n in result.stdout.decode("utf-8").split("\0") if n])
