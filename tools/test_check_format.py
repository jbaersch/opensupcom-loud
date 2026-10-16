"""The formatting ratchet must inspect changes under user diff settings."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

import check_format


class DiffPrefixTest(unittest.TestCase):
    def test_changed_lines_ignore_user_prefix_settings(self):
        previous = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            try:
                os.chdir(directory)
                subprocess.run(["git", "init", "-q"], check=True)
                Path("src").mkdir()
                source = Path("src/example.cpp")
                source.write_text("int value = 0;\n")
                subprocess.run(["git", "add", "src/example.cpp"], check=True)
                subprocess.run(
                    [
                        "git", "-c", "user.name=Format Test",
                        "-c", "user.email=format@example.invalid",
                        "-c", "commit.gpgsign=false", "commit", "-qm", "fixture",
                    ],
                    check=True,
                )
                source.write_text("int value = 1;\n")
                configurations = [(False, False), (True, False), (False, True), (True, True)]
                for mnemonic, no_prefix in configurations:
                    with self.subTest(mnemonic=mnemonic, no_prefix=no_prefix):
                        subprocess.run(
                            ["git", "config", "diff.mnemonicPrefix", str(mnemonic).lower()], check=True
                        )
                        subprocess.run(
                            ["git", "config", "diff.noprefix", str(no_prefix).lower()], check=True
                        )
                        self.assertEqual(
                            check_format.changed_lines("HEAD"), {"src/example.cpp": [(1, 1)]}
                        )
            finally:
                os.chdir(previous)


if __name__ == "__main__":
    unittest.main()
