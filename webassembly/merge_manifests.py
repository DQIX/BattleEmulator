import json
import sys
from pathlib import Path


def main() -> int:
    branches_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("public/branches")
    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("public/emulators.json")

    entries = []
    if branches_dir.exists():
        for manifest in sorted(branches_dir.glob("*/manifest.json")):
            try:
                data = json.loads(manifest.read_text(encoding="utf-8"))
                if isinstance(data, list):
                    entries.extend(data)
            except json.JSONDecodeError:
                continue

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(entries, indent=2), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
