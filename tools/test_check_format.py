"""The formatting ratchet must inspect changes under user diff settings."""

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import check_format


class DiffPrefixTest(unittest.TestCase):
    def test_changed_lines_ignore_user_prefix_settings(self) -> None:
        previous = Path.cwd()
        checker = Path(check_format.__file__).resolve()
        with tempfile.TemporaryDirectory() as directory:
            try:
                os.chdir(directory)
                _ = subprocess.run(["git", "init", "-q"], check=True)
                Path("src").mkdir()
                _ = Path(".clang-format").write_text("BasedOnStyle: LLVM\n")
                source = Path("src/example.cpp")
                _ = source.write_text("int value = 0;\n")
                _ = subprocess.run(["git", "add", "src/example.cpp"], check=True)
                _ = subprocess.run(
                    [
                        "git",
                        "-c",
                        "user.name=Format Test",
                        "-c",
                        "user.email=format@example.invalid",
                        "-c",
                        "commit.gpgsign=false",
                        "commit",
                        "-qm",
                        "fixture",
                    ],
                    check=True,
                )
                configurations = [(False, False), (True, False), (False, True), (True, True)]
                for mnemonic, no_prefix in configurations:
                    with self.subTest(mnemonic=mnemonic, no_prefix=no_prefix):
                        _ = subprocess.run(
                            ["git", "config", "diff.mnemonicPrefix", str(mnemonic).lower()],
                            check=True,
                        )
                        _ = subprocess.run(
                            ["git", "config", "diff.noprefix", str(no_prefix).lower()], check=True
                        )
                        _ = source.write_text("int value=1;\n")
                        malformed = subprocess.run(
                            [sys.executable, str(checker), "HEAD"],
                            capture_output=True,
                            text=True,
                            check=False,
                        )
                        self.assertEqual(
                            malformed.returncode, 1, malformed.stdout + malformed.stderr
                        )
                        self.assertIn("src/example.cpp", malformed.stdout)
                        self.assertEqual(
                            check_format.changed_lines("HEAD"), {"src/example.cpp": [(1, 1)]}
                        )
                        _ = source.write_text("int value = 1;\n")
                        formatted = subprocess.run(
                            [sys.executable, str(checker), "HEAD"],
                            capture_output=True,
                            text=True,
                            check=False,
                        )
                        self.assertEqual(
                            formatted.returncode, 0, formatted.stdout + formatted.stderr
                        )
            finally:
                os.chdir(previous)


if __name__ == "__main__":
    _ = unittest.main()
