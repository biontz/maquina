"""test runner: tools/d50_compare/tests/run.py - stdlib-only (no pytest here).
Usage: python3 tools/d50_compare/tests/run.py"""
import importlib.util
import os
import sys
import tempfile
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), ".."))  # tools/ on path
sys.path.insert(0, os.path.dirname(HERE))

failures = 0
for fname in sorted(f for f in os.listdir(HERE) if f.startswith("test_") and f.endswith(".py")):
    spec = importlib.util.spec_from_file_location(fname[:-3], os.path.join(HERE, fname))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    for name in sorted(dir(mod)):
        if name.startswith("test_"):
            fn = getattr(mod, name)
            try:
                with tempfile.TemporaryDirectory() as td:
                    fn(td)
                print(f"PASS {fname}::{name}")
            except Exception:
                failures += 1
                print(f"FAIL {fname}::{name}")
                traceback.print_exc()
print("OK" if failures == 0 else f"{failures} FAILURES")
sys.exit(1 if failures else 0)