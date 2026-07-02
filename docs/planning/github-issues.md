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
30. Add macOS app artifact build
31. Add tagged release workflow
32. Add Windows build workflow

### Student Workflow UI

33. Add per-service performance controls
34. Add repeated-service creation
35. Add timetable rounding support
36. Add overtaking workflow
37. Keep info pane read-only outside edit mode

### Diagrams And Exports

38. Add train filtering in diagrams
39. Add hover readouts
40. Add timetable graph and table toggle
41. Add diagram export
42. Add table export
43. Add blocking-time timetable overlay
44. Add speed-time diagram
45. Add single-train time-distance diagram

### Capacity And Disruptions

46. Add minimum headway workflow
47. Add timetable compression workflow
48. Add critical-block highlighting
49. Add buffer-time calculation
50. Add capacity percentage output
51. Add signal failure editor
52. Add train breakdown editor
53. Add delay summary output

### Full Infrastructure Editor

54. Add infrastructure edit mode
55. Add node and arc editing
56. Add track and switch editing
57. Add station and platform editing
58. Add signal and block editing
59. Add topology validation

### Memory And Performance

60. Add memory ownership inventory
61. Replace oversized `BlockSet` storage
62. Replace oversized signalling section storage
63. Add memory measurement script
64. Profile GUI and simulation playback
