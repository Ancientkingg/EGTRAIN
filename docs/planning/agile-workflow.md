# Agile Workflow

## Cadence

Use two-week sprints. Each sprint should end with a working demo, test output, and updated GitHub issues.

## Milestones

1. Repository setup
2. Scene model and converter
3. MVP scene editor
4. Assignment case study
5. Student workflow UI
6. Diagrams and exports
7. Capacity and disruptions
8. Full infrastructure editor
9. Memory and performance

## Issue Types

- Epic: milestone-level work.
- Feature: user-visible behavior.
- Bug: incorrect behavior.
- Tech debt: cleanup or modernization.
- Docs: documentation.
- Test: verification work.

## Priority

- P0: blocks the V1 assignment workflow or repository migration.
- P1: needed for a good assignment release.
- P2: useful after V1.

## Sprint Planning

For each sprint:

1. Pick one milestone focus.
2. Pull only issues that have acceptance criteria.
3. Keep one verification issue in the sprint when the work affects simulation, input, or UI behavior.
4. End with a demo using a real case-study scene.

## Definition Of Ready

An issue is ready when it has:

- user or maintainer outcome
- files or subsystem in scope
- acceptance criteria
- verification command
- milestone
- priority

## Definition Of Done

An issue is done when:

- acceptance criteria are met
- implementation is committed
- required tests pass
- docs are updated when behavior changes
- generated files are not committed
- the issue links to a pull request or commit

## Branch Naming

```text
feature/<short-name>
fix/<short-name>
docs/<short-name>
chore/<short-name>
```

## Pull Request Checklist

- What changed?
- Which issue does this close?
- Which commands passed?
- What risks remain?
- Are generated files excluded?

## Verification Policy

For UI changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/e2e/visual_polish_smoke.sh
```

For simulation, scene model, data conversion, or memory changes:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
tools/e2e/headless_smoke.py
tools/e2e/visual_polish_smoke.sh
```
