# AGENTS.md

## Mission

Build the smallest genuinely useful version of this project as quickly as practical without losing correctness, reversibility, or control.

Always follow `docs/process/fast-build-os.md`.

## Before meaningful work

Read, in this order:
1. `PROJECT.md`
2. `DISCOVERY.md`
3. `docs/process/fast-build-os.md`
4. this file
5. relevant current code, tests, and recent related changes

Current repository state is authoritative. Do not rely on stale conversational memory when the repo can answer the question.

## Scope

Work only on the current objective.

Do not silently add related features, cleanup, abstractions, redesigns, dependencies, or refactors.

If unrelated work is discovered:
- note it in the backlog or report it;
- continue the current objective unless it is a blocker, regression, serious safety issue, or proves the current approach invalid.

## Speed rules

- Optimize for a useful V0.1, not completeness.
- Prefer the smallest end-to-end vertical slice.
- Prefer supported platform mechanisms and proven paths over custom inventions.
- Do not create unnecessary architecture for hypothetical future needs.
- Do not add a dependency unless it materially simplifies the current objective and the native platform is insufficient.
- Do not ask the human for evidence unless it materially reduces uncertainty.

## Evidence and uncertainty

For unfamiliar integrations or unclear behavior:
- research authoritative documentation first;
- inspect relevant existing implementation/code when useful;
- distinguish verified facts, observed behavior, inference, and unknowns;
- record durable findings in `DISCOVERY.md`;
- never fabricate environment details, APIs, payloads, file structures, errors, or test results.

When the human supplies screenshots, logs, traces, HAR/network data, or errors, analyze the actual evidence before proposing changes.

Never request secrets, credentials, private tokens, cookies, or unnecessary sensitive data. Tell the human to sanitize captures where needed.

## Risk-first behavior

Before significant implementation, ask whether there is a technical assumption that could invalidate the approach.

If so, prove it with the cheapest useful experiment before building around it.

Do not create exploratory spikes when the mechanism is already sufficiently established.

## Implementation rules

- Make the smallest change that solves the current objective.
- Preserve existing required behavior.
- Do not rewrite unrelated working code.
- Prefer clear, direct code over clever abstractions.
- Keep changes reviewable.
- Handle errors deliberately where they affect the useful path.
- Follow the native conventions and security model of the target platform.

## Debugging stop-loss

For a bug:
1. reproduce or establish evidence;
2. identify the last known-good behavior;
3. inspect relevant logs/state;
4. form a specific hypothesis;
5. make one focused change;
6. retest the failure and relevant existing behavior.

After **two failed speculative fixes**, stop modifying code.

Switch to investigation and determine what evidence or experiment is required to establish root cause. Do not broaden the rewrite in response to uncertainty.

If a narrow bug appears to require a large refactor, stop and reassess the diagnosis and scope first.

## Git rules

- Treat `main` as the known-good accepted state.
- Use a focused branch for meaningful implementation work when practical.
- Keep commits focused and understandable.
- Do not combine unrelated changes.
- Review the diff before integration.
- Do not merge known regressions or failing required behavior.
- Preserve a recoverable checkpoint before risky changes.

## Testing

Testing should be proportional to the project, but every implementation must answer:

> How do we know the intended outcome works?

Prefer the cheapest test that gives real confidence.

For bug fixes, add or perform a regression check whenever practical so the same failure is detectable if reintroduced.

Never claim a test passed unless it was actually run or the human explicitly confirmed the behavior.

## Safety triggers

Apply extra care to:
- authentication/authorization;
- destructive operations;
- user or persistent data;
- migrations;
- secrets;
- payments;
- production systems;
- private/personal data;
- network-exposed endpoints.

Prefer test environments, reversibility, validation, backups, and least privilege where appropriate.

## Completion

A task is complete when:
- the current outcome works;
- acceptance conditions for the current objective are met;
- required existing behavior remains intact;
- the implementation did not silently expand scope;
- important discoveries were persisted in `DISCOVERY.md` or `PROJECT.md`;
- the current working state is recoverable.

Once V0.1 is useful, stop building optional features and let real use determine what comes next.
