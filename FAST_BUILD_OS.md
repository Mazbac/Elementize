# Fast Build OS v1.0

## Purpose

Fast Build OS is optimized for one goal: turn an idea into the smallest genuinely useful working product as quickly as practical, usually targeting hours rather than weeks.

It is designed for AI-assisted development where the human primarily describes the desired outcome, provides evidence from the real environment when needed, tests the result, and gives feedback. The AI handles research, technical reasoning, implementation, debugging, and project-state maintenance.

This is not a heavyweight software-development framework. Do not add process unless it materially improves speed, correctness, reversibility, or safety.

## Core rules

1. **Useful V0.1 first.** Build the smallest end-to-end version that already delivers the intended outcome.
2. **Evidence before implementation.** For unfamiliar integrations or unclear behavior, inspect authoritative documentation and relevant real-world evidence before writing production code.
3. **Proven path before invention.** Prefer supported APIs, official documentation, established platform mechanisms, and validated prior art over custom workarounds.
4. **Riskiest uncertainty first.** Identify the assumption that would waste the most time if wrong and prove it cheaply before building around it.
5. **Vertical slice before breadth.** Make one complete user-to-result path work before expanding features.
6. **Real observations beat AI guesses.** Logs, request/response traces, screenshots, console output, test results, and actual system state outrank speculation.
7. **Two failed speculative fixes means stop.** After two reasonable speculative fixes fail, stop modifying code and switch to root-cause investigation.
8. **One objective at a time.** Do not silently expand scope. New ideas go to the backlog unless they are required to make the current V0.1 work.
9. **Preserve known-good states.** Use Git checkpoints. Do not destroy a working state while experimenting.
10. **Use the product as soon as it works.** Actual use determines the next improvements.
11. **Current repository state beats AI memory.** Re-read current project files and relevant code before making meaningful changes.
12. **Ask the human only for evidence that materially reduces uncertainty.** Do not turn discovery into bureaucracy.

## Fast Build flow

### 1. Define the outcome

Clarify what the human actually wants to be able to do.

Capture in `PROJECT.md`:
- desired user outcome;
- target user;
- current environment if relevant;
- constraints;
- what success looks like today.

Do not start by listing every possible feature.

### 2. Cut to the smallest useful V0.1

Ask: **What is the smallest version that would already be useful today?**

Separate:
- **Must work now**;
- **Later / backlog**;
- **Explicitly out of scope for V0.1**.

If the requested scope cannot realistically become useful quickly, reduce the scope rather than pretending the full system can be delivered in hours.

### 3. Gather only useful evidence

Before coding, determine whether the implementation path is already well understood.

If it is well understood, proceed.

If it is not, gather only evidence that can resolve material uncertainty, for example:
- official documentation;
- exact software/platform versions;
- existing proven implementations;
- relevant source code;
- sanitized network/HAR traces;
- request and response payloads;
- console errors;
- application/server logs;
- screenshots;
- before/after state;
- configuration details.

Never request secrets, raw credentials, private tokens, session cookies, or unnecessary sensitive data. Sanitize evidence first.

Store durable findings in `DISCOVERY.md`.

### 4. Separate facts from guesses

For unfamiliar technical areas, classify important information as:

- **Verified fact** — supported by authoritative documentation or direct validation;
- **Observed behavior** — directly seen in the real system;
- **Inference** — likely explanation based on evidence;
- **Unknown** — not yet established.

Do not present inference as fact.

### 5. Identify the biggest technical risk

Ask:

> What assumption, if wrong, would cause the most wasted implementation time or invalidate the approach?

Examples:
- whether an external system can actually be controlled through the intended interface;
- whether authentication works in the target environment;
- whether data can be modified safely;
- whether a required API is available;
- whether an undocumented mechanism is stable enough to rely on.

### 6. Prove the risky mechanism cheaply

If a critical assumption remains uncertain, perform the smallest possible technical experiment.

This is not the product. It exists only to answer one important question.

If the experiment fails, investigate or change approach before building more code.

If it succeeds, record the finding in `DISCOVERY.md` and proceed.

Do not create a spike when the mechanism is already sufficiently established.

### 7. Design only enough architecture for V0.1

Choose the simplest architecture that supports the current useful outcome safely.

Avoid premature abstractions, generalized frameworks, unnecessary services, speculative extensibility, and dependencies that do not materially help V0.1.

Before adding a dependency, ask whether the platform or standard library already solves the problem.

### 8. Build one vertical slice

Implement one complete path:

`human action -> application -> required integration/backend -> persisted/real result -> visible confirmation`

Prefer an ugly but genuine end-to-end result over many polished disconnected components.

### 9. Test in the real environment

The human tests the actual product where practical.

Useful feedback format:
- what I did;
- what happened;
- what I expected;
- screenshot/log/error if relevant.

The AI should turn this into a reproducible technical problem before changing code.

### 10. Debug with evidence

For a failure:

1. Reproduce it.
2. Establish the last known-good state.
3. Inspect relevant logs/state/errors.
4. Form a specific hypothesis.
5. Make one focused change.
6. Retest the original failure and relevant existing behavior.

If two speculative fixes fail:

**STOP CODING.**

Return to investigation:
- what is actually known;
- what changed;
- what evidence is missing;
- what assumption may be wrong;
- what smallest experiment distinguishes likely causes.

Do not respond to repeated failure with broader rewrites.

### 11. Protect the working state

For meaningful changes:
- start from current `main`;
- use a focused branch where appropriate;
- keep changes small;
- commit known-good checkpoints;
- do not mix unrelated work;
- review the diff before integrating;
- do not merge a change that breaks existing required behavior.

For very early disposable prototypes, Git ceremony may be minimal, but a known-good checkpoint must still exist before risky changes.

### 12. Stop when V0.1 is useful

Once the core outcome works reliably enough for real use, stop expanding the build simply because more features are possible.

Use the product.

### 13. Iterate from real use

Classify new discoveries:

- **Blocker:** prevents the intended outcome -> fix now;
- **Regression:** previously working required behavior broke -> fix now;
- **Important usability problem:** materially impairs use -> prioritize;
- **New feature / improvement:** backlog unless needed now;
- **Cosmetic polish:** later unless it prevents effective use.

Repeat small improvement -> test -> known-good checkpoint -> use.

## Scope-control rules

A new idea does not automatically become current work.

Interrupt current work only when one of these is true:
- it blocks the V0.1 outcome;
- it exposes a meaningful security, privacy, or data-loss risk;
- it proves the current implementation approach fundamentally wrong;
- it is necessary to complete the current vertical slice.

Otherwise capture it under `PROJECT.md` backlog and continue.

## Rabbit-hole stop-loss

Stop implementation and reassess when any of these happen:
- two speculative fixes fail;
- a small task suddenly requires broad unrelated changes;
- the AI proposes a large refactor to fix a narrow bug;
- the same class of regression keeps returning;
- new evidence contradicts the architecture;
- the code is no longer explainable in terms of the intended outcome;
- debugging is consuming more effort than validating a different approach would.

Allowed responses include:
- gather better evidence;
- reproduce in isolation;
- revert;
- discard the branch;
- simplify scope;
- change technical approach;
- abandon the feature.

## Safety triggers

Keep Fast Build lightweight, but increase care when work touches:
- authentication or authorization;
- secrets or credentials;
- destructive actions;
- persistent user data;
- database migrations;
- payments;
- file uploads;
- external network access;
- production systems;
- private/personal data.

For these, prefer reversible changes, backups or test environments where appropriate. Never trade obvious data/security safety for speed.

## AI operating interface

The human should be able to use simple conversational commands such as:

- `Start Fast Build: I want ...`
- `Fast Build status.`
- `Here is the evidence you asked for.`
- `I tested it. This happened ...`
- `New idea: ...`
- `Continue Fast Build.`
- `Does the current approach still make sense?`

The AI is responsible for mapping those messages onto this process. The human should not need to manually invoke every phase.

## Definition of a successful Fast Build

A Fast Build is successful when:
- the smallest intended user outcome works end-to-end;
- the human can actually use it;
- critical assumptions are no longer guesses;
- obvious blockers/regressions are resolved;
- the current working state is recoverable;
- nonessential ideas have not hijacked V0.1;
- remaining work is clearly optional or iterative rather than required to prove usefulness.

The objective is not perfect software. The objective is **the fastest responsible path to a genuinely useful working product**.
