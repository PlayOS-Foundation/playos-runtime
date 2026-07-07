---
name: Implementation Task
about: A scoped task an AI agent or contributor can implement.
title: "Task: "
labels: [implementation, agent-ready]
assignees: []
---

## Goal

<!-- One sentence: what should exist after this task is complete. -->

## Source of truth (read first)

- `AGENTS.md`
- `.github/copilot-instructions.md`
- Relevant `playos-spec` chapters / RFCs:
  <!-- e.g. book/src/08-runtime-architecture/07-package-execution.md -->

## Required output

<!-- Files to create or modify, and the behavior expected. -->

## Acceptance criteria

- [ ] Builds: `cmake -B build && cmake --build build`
- [ ] Cross-platform: shared core + per-OS backends (no OS #ifdef in headers)
- [ ] Matches the spec contract referenced above

## Constraints

- Follow this repository's `AGENTS.md`.
- Keep the change scoped; the compositor stays Linux-only behind its CMake option.
