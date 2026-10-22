#!/usr/bin/env python3
"""Exercise the generated build identity with isolated, data-free Git repositories.

Usage: test_build_identity.py <source directory> <cmake executable>
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path


def check(source: Path, cmake: str) -> None:
    with tempfile.TemporaryDirectory(prefix="osc-build-identity-") as directory:
        root = Path(directory)
        env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
        config = root / "gitconfig"
        _ = config.write_text("", encoding="utf-8")
        env.update(
            GIT_CONFIG_NOSYSTEM="1",
            GIT_CONFIG_GLOBAL=str(config),
            GIT_CEILING_DIRECTORIES=str(root),
            GIT_AUTHOR_NAME="Build Identity Test",
            GIT_AUTHOR_EMAIL="build-identity@example.invalid",
            GIT_COMMITTER_NAME="Build Identity Test",
            GIT_COMMITTER_EMAIL="build-identity@example.invalid",
        )

        def run(*args: str, cwd: Path) -> str:
            return subprocess.run(
                args, cwd=cwd, env=env, check=True, capture_output=True, text=True
            ).stdout.strip()

        def generate(repo: Path, name: str) -> tuple[dict[str, str], Path]:
            header = root / f"{name}.hpp"
            _ = run(
                cmake,
                f"-DSOURCE_DIR={repo}",
                f"-DOUTPUT={header}",
                "-P",
                str(source / "cmake/OscRevision.cmake"),
                cwd=root,
            )
            definitions = dict(
                re.findall(
                    r'^#define (OSC_\w+) "([^"]*)"$',
                    header.read_text(encoding="utf-8"),
                    re.MULTILINE,
                )
            )
            return definitions, header

        repo = root / "full"
        repo.mkdir()
        templates = root / "templates"
        templates.mkdir()
        _ = run("git", "init", "-q", "-b", "main", f"--template={templates}", cwd=repo)
        _ = run("git", "config", "core.autocrlf", "false", cwd=repo)
        _ = run("git", "config", "commit.gpgsign", "false", cwd=repo)
        _ = run("git", "config", "tag.gpgsign", "false", cwd=repo)
        fixture = repo / "fixture.txt"
        _ = fixture.write_text("first\n", encoding="utf-8")
        _ = run("git", "add", "fixture.txt", cwd=repo)
        _ = run("git", "commit", "-qm", "first fixture", cwd=repo)
        _ = run("git", "tag", "-a", "v0.1.0", "-m", "fixture release", cwd=repo)
        _ = fixture.write_text("second\n", encoding="utf-8")
        _ = run("git", "commit", "-qam", "second fixture", cwd=repo)
        head = run("git", "rev-parse", "HEAD", cwd=repo)

        shallow = root / "shallow"
        _ = run("git", "clone", "-q", "--depth=1", repo.as_uri(), str(shallow), cwd=root)
        assert run("git", "rev-parse", "--is-shallow-repository", cwd=shallow) == "true"
        assert run("git", "rev-parse", "HEAD", cwd=shallow) == head

        # The display revision may use a reachable release tag and abbreviations.
        # The identity must depend only on HEAD, independent of either setting.
        for checkout in (repo, shallow):
            for abbreviation in (7, 8, 12):
                _ = run("git", "config", "core.abbrev", str(abbreviation), cwd=checkout)
                definitions, _ = generate(checkout, f"{checkout.name}-{abbreviation}")
                assert definitions.get("OSC_SOURCE_ID") == head, definitions
                assert definitions["OSC_REVISION"] == run(
                    "git", "describe", "--always", "--dirty", cwd=checkout
                )

        _ = run("git", "tag", "-a", "v0.2.0", "-m", "same source, new tag", cwd=repo)
        tagged, header = generate(repo, "stable")
        assert tagged["OSC_SOURCE_ID"] == head
        assert tagged["OSC_REVISION"] == "v0.2.0"

        # A generated header that has not changed must not rebuild version.cpp.
        old_time = 1_000_000_000_000_000_000
        os.utime(header, ns=(old_time, old_time))
        timestamp = header.stat().st_mtime_ns
        repeated, _ = generate(repo, "stable")
        assert repeated == tagged
        assert header.stat().st_mtime_ns == timestamp

        # Changes to filesystem timestamps and untracked files are not source changes.
        os.utime(fixture, ns=(old_time, old_time))
        _ = (repo / "untracked.txt").write_text("local output\n", encoding="utf-8")
        unchanged, _ = generate(repo, "untouched")
        assert unchanged["OSC_SOURCE_ID"] == head

        _ = fixture.write_text("third\n", encoding="utf-8")
        unstaged, _ = generate(repo, "unstaged")
        assert unstaged["OSC_SOURCE_ID"] == head + "-dirty"
        _ = run("git", "add", "fixture.txt", cwd=repo)
        staged, _ = generate(repo, "staged")
        assert staged["OSC_SOURCE_ID"] == head + "-dirty"
        _ = run("git", "commit", "-qm", "third fixture", cwd=repo)
        new_head = run("git", "rev-parse", "HEAD", cwd=repo)
        committed, _ = generate(repo, "committed")
        assert new_head != head
        assert committed["OSC_SOURCE_ID"] == new_head

        outside = root / "outside-git"
        outside.mkdir()
        unknown, _ = generate(outside, "unknown")
        assert unknown == {"OSC_REVISION": "unknown", "OSC_SOURCE_ID": "unknown"}


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__)
        return 2
    check(Path(argv[1]).resolve(), argv[2])
    print("build identity: isolated Git regression checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
