# Fast Build OS Template

A lightweight template for building small, useful AI-assisted software products quickly.

## Goal

Get to a genuinely useful V0.1 as fast as practical without letting AI drift into unnecessary scope, speculative debugging, or large rewrites.

## Included files

- `FAST_BUILD_OS.md` — the development process and stop-loss rules.
- `AGENTS.md` — operating instructions for coding agents such as Codex.
- `PROJECT.md` — the current project's outcome, V0.1 scope, backlog, and working state.
- `DISCOVERY.md` — verified technical knowledge, evidence, unknowns, and validated decisions.

## Starting a new project

1. Create a new repository from this GitHub template.
2. Open/use the new repository with ChatGPT/Codex.
3. Start with:

   `Start Fast Build: I want [desired outcome].`

4. Let the AI update `PROJECT.md` as the outcome and V0.1 are clarified.
5. If the implementation path is uncertain, let the AI research first and update `DISCOVERY.md`.
6. Provide screenshots, logs, sanitized network traces, or other real-world evidence only when requested and useful.
7. Let Codex implement the smallest end-to-end vertical slice.
8. Test the real result and report what happened.
9. Once V0.1 is useful, use it before expanding the scope.

## Typical conversation

**You:** `Start Fast Build: I want a tool that ...`

**AI:** Defines the smallest useful outcome, checks whether the technical path is known, researches or asks for targeted evidence if necessary, then scopes the implementation.

**You:** `Here is the evidence you asked for.`

**AI:** Analyzes it, records durable findings, validates the critical path, and proceeds when the approach is sufficiently established.

**You:** `I tested it. This happened ...`

**AI:** Reproduces/diagnoses from evidence and makes a focused fix. After two failed speculative fixes, it stops coding and investigates instead of thrashing.

**You:** `New idea: ...`

**AI:** Adds it to the backlog unless it is required for the current useful outcome.

## Human role

You do not need to be the developer. Your main responsibilities are:
- describe what you want to achieve;
- provide access/context about the real environment;
- perform simple evidence-gathering steps when useful;
- test the resulting product;
- report what actually happened.

## AI role

The AI should:
- research before guessing;
- prefer proven/supported paths;
- keep V0.1 small;
- prove dangerous assumptions early;
- implement in small vertical slices;
- preserve known-good states;
- diagnose with evidence;
- prevent unrelated ideas from hijacking the build;
- keep `PROJECT.md` and `DISCOVERY.md` current.

## Core principle

> The fastest responsible path to a genuinely useful working product.
