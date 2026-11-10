#!/usr/bin/env python3
"""Run and verify executable code blocks in docs/GUIDE.md."""

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
GUIDE_PATH = REPO_ROOT / "docs" / "GUIDE.md"
CFIRE_BIN = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else (REPO_ROOT / "cfire")

if not CFIRE_BIN.exists():
    print(f"Error: cfire binary not found at {CFIRE_BIN}", file=sys.stderr)
    sys.exit(1)

if not GUIDE_PATH.exists():
    print(f"Error: GUIDE.md not found at {GUIDE_PATH}", file=sys.stderr)
    sys.exit(1)

with open(GUIDE_PATH, "r", encoding="utf-8") as f:
    text = f.read()

pattern = re.compile(r"```cfire\n(.*?)\n```\s*```text\n# Output:\n(.*?)\n```", re.DOTALL)
pos = 0
matches = []
while True:
    m = pattern.search(text, pos)
    if not m:
        break
    code = m.group(1)
    if "```cfire" in code:
        last_cfire = code.rfind("```cfire")
        actual_code = code[last_cfire + len("```cfire\n"):]
        matches.append((actual_code, m.group(2)))
    else:
        matches.append((code, m.group(2)))
    pos = m.end()

print(f"=== Found {len(matches)} testable GUIDE.md code blocks ===\n")

passed = 0
failed = 0

for i, (code, expected_output) in enumerate(matches, 1):
    expected_output = expected_output.strip()

    with tempfile.NamedTemporaryFile(suffix=".cf", delete=False, mode="w", encoding="utf-8") as tf:
        tf.write(code)
        tf_name = tf.name

    try:
        res = subprocess.run([str(CFIRE_BIN), "run", tf_name], capture_output=True, text=True, timeout=20)
        actual_output = (res.stdout + ("\n" + res.stderr if res.stderr else "")).strip()

        ok = (res.returncode == 0) and (expected_output in actual_output or actual_output == expected_output)

        if ok:
            print(f"  [{i:02d}/{len(matches):02d}] PASS")
            passed += 1
        else:
            print(f"  [{i:02d}/{len(matches):02d}] FAIL (exit code {res.returncode})")
            if res.returncode != 0:
                err = res.stderr.strip()
                if len(err) > 300:
                    err = err[:300] + "..."
                print(f"       STDERR: {err}")
            else:
                print(f"       EXPECTED: {expected_output}")
                print(f"       ACTUAL:   {actual_output}")
            failed += 1
    except subprocess.TimeoutExpired:
        print(f"  [{i:02d}/{len(matches):02d}] TIMEOUT")
        failed += 1
    except Exception as e:
        print(f"  [{i:02d}/{len(matches):02d}] ERROR: {e}")
        failed += 1
    finally:
        if os.path.exists(tf_name):
            os.remove(tf_name)

print(f"\n==========================================")
print(f"GUIDE Doc-Test Results: {passed} passed, {failed} failed (total {len(matches)})")

if failed > 0:
    sys.exit(1)
