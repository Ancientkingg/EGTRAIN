# GitHub Issue Plan

## Milestones

1. Repository setup
2. Scene model and converter
3. MVP scene editor
4. Assignment case study
5. CI/CD pipeline
6. Student workflow UI
7. Diagrams and exports
8. Capacity and disruptions
9. Full infrastructure editor
10. Memory and performance

## Labels

- `type: epic`
- `type: feature`
- `type: bug`
- `type: tech debt`
- `type: docs`
- `type: test`
- `area: scene model`
- `area: converter`
- `area: editor`
- `area: assignment`
- `area: ci`
- `area: ui`
- `area: diagrams`
- `area: capacity`
- `area: disruptions`
- `area: memory`
- `priority: p0`
- `priority: p1`
- `priority: p2`

## Initial Backlog

### Repository Setup

1. Clean repository layout
2. Add build and test documentation
3. Add issue templates
4. Add workflow and roadmap docs
5. Add CI build and test workflow

### Scene Model And Converter

6. Define canonical scene schema
7. Implement scene validator
8. Implement legacy scene importer
9. Implement legacy scene exporter
10. Add Copenhagen conversion test
11. Add Brescia conversion test
12. Add validation diagnostics model

### MVP Scene Editor

13. Design scene editor window flow
14. Add scene load and save UI
15. Add validation panel
16. Add train composition editor
17. Add service editor
18. Add timetable editor
19. Add incident editor
20. Add editor smoke test

### Assignment Case Study

21. Audit Netherlands input data
22. Decide final assignment corridor
23. Build `Gvc-Gdg-Ut` scene
24. Add assignment timetable data
25. Add assignment smoke test

### CI/CD Pipeline

26. Expand CI to run headless smoke tests
27. Add visual smoke test to CI
28. Upload failure logs and screenshots
29. Add repository hygiene workflow
30. Add cross-platform release matrix
31. Package Windows release artifact
32. Package macOS release artifact
33. Package Linux release artifact
34. Attach release artifacts to tagged GitHub releases
35. Document release testing checklist

### Student Workflow UI

36. Add per-service performance controls
37. Add repeated-service creation
38. Add timetable rounding support
39. Add overtaking workflow
40. Keep info pane read-only outside edit mode

### Diagrams And Exports

41. Add train filtering in diagrams
42. Add hover readouts
43. Add timetable graph and table toggle
44. Add diagram export
45. Add table export
46. Add blocking-time timetable overlay
47. Add speed-time diagram
48. Add single-train time-distance diagram

### Capacity And Disruptions

49. Add minimum headway workflow
50. Add timetable compression workflow
51. Add critical-block highlighting
52. Add buffer-time calculation
53. Add capacity percentage output
54. Add signal failure editor
55. Add train breakdown editor
56. Add delay summary output

### Full Infrastructure Editor

57. Add infrastructure edit mode
58. Add node and arc editing
59. Add track and switch editing
60. Add station and platform editing
61. Add signal and block editing
62. Add topology validation

### Memory And Performance

63. Add memory ownership inventory
64. Replace oversized `BlockSet` storage
65. Replace oversized signalling section storage
66. Add memory measurement script
67. Profile GUI and simulation playback
