#!/usr/bin/env python3
"""Regression checks for conformance-tool failure propagation."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
TOOLS = ROOT / "cpp/tools"
FIXTURE = "tsc/testdata/tests/cases/compiler/unusedVariablesinNamespaces1.ts"


class ConformanceFailures(unittest.TestCase):
    def test_missing_compilers_fail_single_file_and_batch_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = str(Path(directory) / "missing-compiler")
            file_list = Path(directory) / "files.list"
            file_list.write_text(FIXTURE + "\n")
            unterminated_list = Path(directory) / "unterminated.list"
            unterminated_list.write_text(FIXTURE)
            env = {**os.environ, "TSCPP": missing, "PARSEDUMP": missing,
                   "BINDUMP": missing}
            for name, arguments, cwd in (
                ("conformance_parse.sh", [FIXTURE], ROOT),
                ("conformance_bind.sh", [FIXTURE], ROOT),
                ("conformance_bind.sh", [str(file_list), "2"], ROOT),
                ("conformance_bind.sh", [str(unterminated_list), "2"], ROOT),
                ("conformance_bind.sh", [file_list.name, "2"], directory),
            ):
                with self.subTest(tool=name, arguments=arguments):
                    result = subprocess.run(["bash", str(TOOLS / name), *arguments],
                                            cwd=cwd, env=env, capture_output=True,
                                            text=True, timeout=10)
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertNotIn("PASS ", result.stdout)
                    self.assertIn("FAIL ", result.stdout)

    def test_empty_binder_lists_are_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            file_list = Path(directory) / "files.list"
            for contents in ("", "\n"):
                with self.subTest(contents=contents):
                    file_list.write_text(contents)
                    result = subprocess.run(
                        ["bash", str(TOOLS / "conformance_bind.sh"), str(file_list), "2"],
                        cwd=ROOT, capture_output=True, text=True, timeout=10)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertNotIn("PASS ", result.stdout)

    def test_empty_lexer_corpus_is_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            result = subprocess.run(["python3", str(TOOLS / "conformance_lex.py"), directory],
                                    cwd=ROOT, capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("empty corpus", result.stderr)


if __name__ == "__main__":
    unittest.main()
