"""Run the C host test of chase_build (tests/test_chain.c) from unittest, so
`python build.py test` covers it wherever a host C compiler exists."""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
# HOSTCC picks the compiler (as in tools/quality.sh); else the first of cc, gcc, clang on PATH.
CC = os.environ.get("HOSTCC") or next((c for c in ("cc", "gcc", "clang") if shutil.which(c)), None)


@unittest.skipIf(CC is None, "no host C compiler")
class ChainHostTest(unittest.TestCase):
    def test_chain_matches_published_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = os.path.join(tmp, "test_chain")
            subprocess.run([CC, "-std=c11", "-O1", "-I", os.path.join(ROOT, "src"),
                            os.path.join(ROOT, "tests", "test_chain.c"),
                            os.path.join(ROOT, "src", "bench_chain.c"), "-o", exe], check=True)
            run = subprocess.run([exe], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stdout)
        self.assertIn("PASS", run.stdout)


if __name__ == "__main__":
    unittest.main()
