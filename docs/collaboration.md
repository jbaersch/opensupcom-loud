# Contributing together

This fork is a LOUD compatibility and playtesting branch of OpenSupCom. General
engine fixes should become small upstream contributions, while LOUD integration
and its acceptance remain explicit work here. Upstream maintainers decide what
lands in the original project; a fork does not reserve an area of development.

## Roles and work queue

The project owner supplies requirements, priorities, gameplay observations and
acceptance decisions. The coding assistant handles implementation, technical
checks, commits, issue/PR maintenance, project communication and documentation
within the authorized work. Technical choices should not require the owner to
learn programming. Scope and intended gameplay changes remain explicit decisions.

Use this fork's Issues as the task queue. Useful labels are `upstream-candidate`,
`loud`, `needs-validation`, `needs-playtest` and `blocked-upstream`. A task states
the user-visible problem, acceptance conditions, current branch/commit and next
action. Existing upstream work is linked when coordination is actually needed;
do not create duplicate reports or automatic comment streams.

Start with one upstream proposal at a time, at most two while they are easy to
review. Respond to feedback before opening more. Supporting another contributor
with a reproducer, test or review is also a useful contribution.

## Branch and validation boundaries

Follow [the fork workflow](fork-workflow.md). Upstream fixes start from
`upstream/main`; LOUD integration work starts from `codex/loud`. Use separate
worktrees, stage exact files and preserve published history. Review the complete
proposed diff, not only the most recent commit.

Before preparing a fix, check the current upstream source, open PRs and relevant
discussion. Distinguish identical fixes, related behavior, textual conflicts and
contradictory assumptions. In particular, a passing synthetic test does not prove
that an invented behavior matches the original game.

Use the smallest useful regression first, then the applicable contribution gates.
Run the failing case before the fix when practical. Report exact commands and
results, including skips, inherited baseline failures and untested platforms.
Bundle a coherent change before pushing; a repeated unchanged green suite adds
no evidence. Documentation-only work needs content, link and diff checks.

For playtesting, provide an identified build, a few concrete steps, expected
behavior, known limits and a way back to the previous accepted build. Keep game
data, captures, saves and raw logs local. Never replace the accepted launcher
build merely because a new source branch compiles.

## Compact handoff for people and agents

Put the relevant facts in the issue/PR or its linked design note. Keep the handoff
short enough to read before working; it must not depend on chat memory.

```text
Goal / observed failure:
Base and head commits; related issue/PR:
Behavioral source and invariants:
Change and files:
Validation: exact command -> result (including RED/GREEN and skips):
Limits / open question / next action:
AI assistance: tool and verified model; unknown if not recorded:
```

AI assistance should be disclosed without treating a model name as a quality
claim. Record generation and review separately if they used different tools or
models. Git author metadata alone does not establish the model. Preserve original
authorship; never retroactively guess a model or invent an independent review.
Do not paste private prompts, machine paths, credentials or game data into public
handoffs. Other contributors are welcome to use their own tools and conventions.

## Communication and monitoring

Use the existing upstream Discussions for scope questions and general exchange.
One concise update is preferable to several new threads or broad promises. Ask
whether LOUD work would be useful; do not assume the maintainers focus only on
FAF, and offer a contribution rather than claiming ownership of LOUD support.
Tool and skill questions can follow naturally if the contributors want to share.

Check upstream at the start of each work package. Recurring monitoring requires
an explicitly configured job; this guide does not activate one. Monitoring should
report actionable changes and request integration through the normal development
workflow, rather than changing the accepted build automatically.
