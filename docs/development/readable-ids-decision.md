# Readable ids decision

This page records how the program shows the ids of blocks, sections and signals, the options that were compared and what each costs. Decided in #1075 on 2026-10-22. Paths are relative to `EGTRAIN/QEGTRAIN` and line numbers are those of `40b8f778`, before #1076 and #1077.

## Decision

| Question | Choice |
| --- | --- |
| Ids | Unchanged. Scene files, legacy import and export, recovery copies, golden files, CSV exports and the live frame keep the ids they have today. |
| What the user reads | A label that the program derives from the scene. The id stays in the tooltip, in a Copy action and, in validation messages, in the text. |
| Block section | The track and the kilometres of the two ends: `B0 21.010 to 21.310 km`. |
| Switch section | The piece on each track: `B0 21.010 to 21.235, B1 21.252 to 21.450 km`. |
| Authored labels | None in this step. |
| Exports | No label column in this step. The id stays the only key. |
| Track number of a run | The scene track id, such as `B10`, with the runtime number in the tooltip. |

## The problem

On Paimpol:

1. Click a signal head in the preview. The inspector title is "Preview: Signal @0-B0@ (not running)" and its details read "Track B0; section @0-B0@; forward; authored signal IDs: None" (`widgets/InspectorPanel.cpp:447` to `:455`).
2. In **Editors > Incidents**, tab **Incidents**, click **Add Incident**. The **Target** list of the signal failure holds the 140 block ids `0-B0`, `1-B0` and so on, with no position or station (`editors/IncidentPanel.cpp:39` to `:50` and `:425`).
3. Run the case and choose **Diagrams > Blocking time...** with `route0`. The block lists read "1:@0-B0@", "2:@1-B0@" and so on, and entry 72 is "72:@71-B0@-21.235000/@0-B1@-21.252000" (`diagrams/BlockingTimeScope.cpp:91` and `:92`).
4. During the run, click a signal on the main line. **Signalling Info** shows **Track ID** 0 for the track that the preview calls B0 (`widgets/InspectorPanel.cpp:678`).

### How the ids are formed

| Id | Example | Formed in |
| --- | --- | --- |
| Track | `B0` | `B` and the number of the legacy folder (`scene/SceneImporter.cpp:1076`) |
| Node, arc | `B0.node.1`, `B0.arc.100` | the track and the first column of `NodiCumPari.txt` or `ArchiCumPari.txt` (`:1110`, `:1164`) |
| Block | `0-B0`, `71-B0` | the row of `BlockCumPari.txt`, counted from 0, and the track (`:1205`) |
| Block section | `@0-B0@` | the block id between `@` signs (`scene/SectionInventory.cpp:15` to `:19`, `:329`) |
| Switch section | `@71-B0@-21.235000/@0-B1@-21.252000` | the two blocks that hold the nodes of a connection, each followed by `-` and the kilometre of its node as printf `%f` writes it (`:169` to `:177`, `:363` to `:377`) |
| Signal head | the id of the section it protects | no committed scene has a signal, so every head uses its section id (`graphics/NetworkPresenter.cpp:666`) |

The `-` before a kilometre is a separator, not a minus sign. `@71-B0@-21.235000/@0-B1@-21.252000` is block 71 of track B0 up to the node at 21.235 km, then block 0 of track B1 from 21.252 km. A block that the editor adds is called `block`, `block_2` and so on (`editors/infrastructure/BlockRouteFacets.cpp:121`, `:149`), so not every block id holds a number.

The run numbers its tracks by their place in the list of track ids sorted as text (`simulation/builders/InfrastructureBuilder.cpp:206` to `:208`, `:274` to `:276`, `:660`), and **Track ID**, **Next track** and the signal tooltips show that number. It matches the digits of the id only in scenes with at most ten tracks. In Netherlands 266 of the 268 tracks get another number: B10 is track 2 and B100 is track 3. In Copenhagen 166 of 168 differ and in Milano_Brescia 36 of 38.

### The ids are data

- The run splits section ids on `@` and `/` and takes the parts by position: `simulation/Signalling.cpp:633` to `:660`, `diagrams/BlockingTimeDiagram.cpp:31` to `:46` and `scene/SectionInventory.cpp:137` to `:145`. The importer and the exporter read route tokens the same way (`scene/SceneImporter.cpp:1216` to `:1244`, `scene/SceneExporter.cpp:66` to `:175`). A search of the product sources outside `tests/` for the literals `'@'` and `"@"` finds 78 lines in 8 files, 41 of them in `Signalling.cpp`.
- Scene files hold both forms. Paimpol `route0` lists `0-B0` to `70-B0` and then `@71-B0@-21.235000/@0-B1@-21.252000`. The six committed scenes hold 25,730 block-id occurrences.
- 127 of the 134 files in `tests/characterization/expected` contain a block or section id, 2,338 in all (`grep -lE '[0-9]+-B[0-9]+'`). The CSV exports, `BlockingTimes.txt` and `section_id` in the live frame (`io/LiveFrame.cpp:54`) contain them too.

## The committed scenes

Counted with a short Python program that repeats the layout of `buildSceneSectionInventory` (`scene/SectionInventory.cpp:271` to `:419`) on the JSON files. The four converted case studies are Paimpol, Netherlands, Copenhagen and Milano_Brescia. Amsterdam_Hilversum_Student has the same infrastructure, stations and signalling files as Netherlands, so it is not listed.

| Scene | Tracks | Blocks | Switch sections | Stations | Tracks with a platform | Blocks between two stations of their track |
| --- | --- | --- | --- | --- | --- | --- |
| Paimpol | 6 | 140 | 8 | 10 | 6 | 122 |
| Netherlands | 268 | 1,984 | 714 | 41 | 80 | 684 |
| Copenhagen | 168 | 1,513 | 328 | 94 | 25 | 1,136 |
| Milano_Brescia | 38 | 312 | 110 | 29 | 35 | 175 |
| Assignment_Gvc_Gdg_Ut | 2 | 64 | 0 | 3 | 2 | 64 |

- Every track has one chain of nodes with `x_km`, so every block can be placed by its track and kilometres.
- A station cannot place every block. In Netherlands 581 blocks lie on the 188 tracks without a platform, and 6 stations have only a position, no platform.
- Every station has a name equal to its id, often a code such as `Ut`, `Utzl` or `Gvc`.
- Netherlands has 714 switch sections for 708 connections. Where a connection node lies on a block boundary, the connection makes a switch section with the block on each side.
- No scene has a signal, so every signal head shows a section id.
- The longest block id has 7 characters (`10-B100`), its block section id 9 and the longest switch section id 39.

## Options and their cost

| | (a) Labels beside unchanged ids | (b) Readable ids with a scene migration | (c) Tooltips and a glossary entry only |
| --- | --- | --- | --- |
| What the user reads | Labels in lists, fields, titles, the chart strip and validation messages | New ids in the same places | The same ids; the decoded form only on hover |
| Product files | About 25 (17 `.cpp`), listed in the plan below | The 12 files that parse ids, the importer, the exporter, a migration step, and the display sites of (a) if the lists should read well | About 12, the same places as (a) without visible texts, plus the glossary |
| Tests | Expected texts in 7 unit test files and 4 end-to-end sources; new cases for the label | 127 golden files recorded again; 1,913 ids in 6 fixture files; 284 ids in 11 test sources | Few, tooltips are seldom checked |
| Data | None | 25,730 block-id occurrences in the committed scenes rewritten by a step from schema 1 to 2 | None |
| Compatibility | No file, export or frame changes | Every saved scene needs the step; recovery copies of schema 1 are still listed but can no longer be opened (`OtherSceneVersion`, `recovery/RecoveryStore.h:44`, `recovery/RecoveryPrompt.cpp:44` and `:45`); CSV keys and `section_id` values change; the legacy export must map the new ids back to row numbers | None |
| Size, estimated | 250 to 350 changed product lines and 150 to 250 test lines | 1,500 lines or more of code and tests, plus the regenerated scenes and golden files | About 100 lines and the glossary entry |

Notes:

- (a) The test files are `test_editor_infrastructure.cpp`, `test_editor_infrastructure_blocks.cpp`, `test_editor_incident.cpp`, `test_diagramwindow.cpp`, `test_blockingtimediagram.cpp`, `test_scenevalidator.cpp` and `test_completedrun.cpp`, and in `tests/e2e` `Captures.cpp`, `CreatorAcceptance.cpp`, `EditorSmoke.cpp` and `PostRun.cpp`. They were found by searching `tests/` for the texts that change. The size is about 45 call sites at 3 to 6 lines each, plus about 60 lines for the label function.
- (b) The production migration registry is empty today (`scene/SceneMigration.cpp:196` to `:199`) and the schema version is 1 (`scene/SceneModel.h:14`), so this would be the first schema step. A readable block id such as `B0.block.71` would still be wrapped and joined by the run (`SectionInventory.cpp:376` and `:377`, `Signalling.cpp:633` and `:634`), so a switch section would read `@B0.block.71@-21.235000/@B1.block.0@-21.252000` unless the runtime form changes too, with every parser above.
- (c) A tooltip needs the decoded form of the id, so (c) needs the same label function as (a). It leaves the lists of 140 to 2,698 bare ids, the strip and the validation messages as they are.

## Why labels

- (a) fixes what the four Paimpol steps show without touching data, so nothing in the scenes, the legacy files, the golden files, the exports or the live frame can break.
- (a) costs little more than (c), because both need the label function and (a) only uses it in more places.
- (b) crosses the data-loss boundaries of the repository: scene files, a first migration step, the legacy export and recovery. It changes 127 golden files, which hides real behaviour changes in a large diff, and still leaves switch sections hard to read.

Against (a):

- One thing has two names. A label is not a reference: it changes when an earlier block of the same track changes length, and the id does not. Notes, bug reports and scripts use the id, which the tooltip and the Copy action give.
- A block label is about twice as wide as a block id, so the chart strip shows fewer labels at one zoom.
- The run inspector needs labels during the run, so the presenter builds them once per run from the scene of the run.
- Validation messages get longer.

None of these blocks the decision.

## The label

### Rules

1. A block section reads `<track> <start> to <end> km`. Start and end are the `startKm` and `endKm` of its descriptor (`scene/SectionInventory.cpp:332` and `:333`), which follow the layout of the run, including a last block that is extended to the track end (`:320` to `:323`) or cut at it (`:310` to `:313`).
2. A switch section reads `<first track> <start> to <first node>, <second track> <second node> to <end> km`: the part of the first block up to the connection, then the part of the second block from it. These are the nodes of the section in order (`:389` to `:397`). A piece whose two ends round to the same value shows that value once, as in `B104 58.000 to 58.099, B100 58.152 km`. Six switch sections of Netherlands and one of Milano_Brescia have such a piece, and no route uses them.
3. Kilometres have three decimals, which is 1 m, and a full stop as the decimal mark in every locale. The shortest block of the committed scenes is 2 m (Netherlands).
4. The track is the scene track id. A track has no other name: `infrastructure.json` gives tracks only an `id`.
5. When two sections would get the same label, both labels end with the id in parentheses. No section of the committed scenes needs this. It can happen with blocks shorter than 0.5 m, since validation only asks for a positive length (`scene/SceneValidator.cpp:729`).
6. A reference that names no section, or more than one, keeps its id as the label. This covers an unknown block and a block whose track has no single chain of nodes, for which the inventory makes no section (`scene/SectionInventory.cpp:35` to `:54`, `:79`, `:297`), and a runtime id that two sections share, which validation reports (`scene/SceneValidator.cpp:1815` to `:1819`) and for which `exact()` finds no section (`scene/SectionInventory.cpp:179` to `:188`).
7. A signal with an authored id shows that id, then the label of its section. A head without an authored signal shows the label of its section.
8. A run shows the scene track id from `BlockSet::sceneTrackId` (`simulation/Infrastructure.h:242`) where it shows a track number today. The number moves to the tooltip.

The label holds no block number. The number in an imported id is the row in the legacy file. Editor ids hold none, and inserting, moving or deleting a block (`editors/infrastructure/BlockRouteFacets.cpp:111` to `:164`) changes the place of the blocks after it but not their ids. A number in the label would then either differ from the id or follow two rules. The kilometres come from the layout that the run uses, so they stay right after any edit. In all committed scenes the number in the id is still the place of the block on its track, counted from 0.

The label holds no station. Station names equal the station ids, which in Netherlands are codes such as `Ut`, and there only 684 of 1,984 blocks lie between two stations of their own track. A missing station name therefore changes nothing in the label.

### Examples from the committed scenes

| Scene | Id | Label |
| --- | --- | --- |
| Paimpol | `@0-B0@` | `B0 0.000 to 0.230 km` |
| Paimpol | `@70-B0@` | `B0 20.710 to 21.010 km` |
| Paimpol | `@71-B0@-21.235000/@0-B1@-21.252000` | `B0 21.010 to 21.235, B1 21.252 to 21.450 km` |
| Paimpol | `@0-B1@` | `B1 21.150 to 21.450 km` |
| Netherlands | `@0-B32@` | `B32 1.254 to 1.560 km` |
| Netherlands | `@1-B32@-1.577000/@0-B36@-1.582000` | `B32 1.560 to 1.577, B36 1.582 to 1.587 km` |
| Copenhagen | `@2-B8@` | `B8 22.140 to 22.400 km` |
| Copenhagen | `@55-B8@-38.100000/@0-B157@-38.135000` | `B8 38.023 to 38.100, B157 38.135 to 38.140 km` |
| Milano_Brescia | `@1-B4@-11.140000/@1-B2@-11.240000` | `B4 10.156 to 11.140, B2 11.240 to 12.543 km` |
| Milano_Brescia | `@1-B4@-11.340000/@1-B2@-11.420000` | `B4 10.156 to 11.340, B2 11.420 to 12.543 km` |
| Assignment_Gvc_Gdg_Ut | `@0-B0@` | `B0 0.000 to 2.000 km` |

The two Milano_Brescia rows are two connections between the same two blocks. A label with only the block ends would be the same for both, which is why a switch label names the nodes of its connection.

Every section of each committed scene gets its own label. Block labels have at most 24 characters and switch labels at most 47.

### Width

- Chart strip: a label is cut in the middle at 240 px (`diagrams/DiagramWindow.cpp:408`), and a label that collides with its neighbour is left out (`:406` to `:417`). Measured with Pillow and the macOS system font at 13 px, with an order number of three digits in front: block labels take at most 180 px and fit, switch labels at most 317 px. The switch ids that they replace take at most 318 px, so switch labels are cut as the ids are today. Block ids with the same number in front take 83 to 99 px.
- Choosers: a dialog opens 480 px wide or 32 line heights, whichever is wider (`widgets/DialogLayout.h:184`), and widens to its content up to nine tenths of the screen (`:181`, `:194`). A `ChoiceComboBox` cuts a longer text in the middle and opens its list as wide as its widest entry within the same limit (`widgets/ChoiceComboBox.h:47`, `:62`). Since a switch label is as wide as its id, the **Blocking time** and **Capacity by area** choosers keep the width they have today. An entry of the **Capacity** chooser holds two labels and grows: for `route0` from about 235 px to 410 px, which stays within those limits.
- Fonts of Windows and Linux were not measured.

## Where labels appear and where ids stay

| Place | Today | With labels | Where the id stays |
| --- | --- | --- | --- |
| Preview inspector of a signal (`widgets/InspectorPanel.cpp:436` to `:455`) | Title and fields with `@0-B0@` | "Preview: Signal B0 0.000 to 0.230 km (not running)"; **Protected section** and **ID of Block Section** show the label | Field tooltips; "section @0-B0@" in the details |
| **Signalling Info** of a run (`:678` to `:698`) | **Track ID** 0, sections by id, **Next track** 0 | `B0`, labels | Tooltips: runtime track number and section id |
| Node, arc and connection info (`:479`, `:525`, `:574`, `:616`, `:645`, `:646`) | Runtime track numbers, section ids | Scene track ids, labels | Tooltips |
| Signal tooltips and **Find** (`graphics/items/SignalItem.cpp:73`, `graphics/NetworkPresenter.cpp:237`, `:262`, `:666`, `app/MainWindow.cpp:4359`) | "Track 0 at 0.23 km; ahead @0-B0@; behind none" | "Track B0 at 0.23 km; ahead B0 0.000 to 0.230 km; behind none" | The inspector |
| Context menu of a signal (`app/MainWindow.cpp:3268` to `:3282`, `:3426` to `:3431`) | **Copy canonical ID**, **Copy signal location** | Both unchanged; the signal menu of a run gains **Copy section ID** | The copied text |
| **Target** of a signal failure (`editors/IncidentPanel.cpp:425` to `:429`) | Block ids | Labels; the item data stays the id, which `commitTarget` saves (`:622`) | Item tooltip |
| Section choices of the infrastructure editor (`editors/InfrastructurePanel.cpp:558` to `:566`) | "base block 0-B0 / track B0", "connection connection.0 / 71-B0 -> 0-B1" | Labels, with the "(legacy: ...)" and "Invalid: ..." forms kept | Item tooltip; the **Blocks** table, where the id is edited |
| **Blocking time** chooser (`diagrams/BlockingTimeScope.cpp:91`, `:92`) | "72:@71-B0@-21.235000/@0-B1@-21.252000" | "72: B0 21.010 to 21.235, B1 21.252 to 21.450 km" | Item tooltip |
| **Capacity** chooser (`diagrams/CapacityScope.cpp:345` to `:352`) | "1:@0-B0@ to 125:@124-B0@ (125 blocks)" | "1: B0 0.000 to 0.230 km through 125: B0 36.910 to 37.062 km (125 blocks)"; "through", since a label holds "to" | Item tooltip |
| **Capacity by area** chooser (`diagrams/AreaCapacityWindow.cpp:629`) | As **Blocking time** | As **Blocking time** | Item tooltip |
| Window subtitles (`app/ResultViews.cpp:521` to `:525`; `diagrams/CapacityScope.cpp:226`, `:227`) | "route0 / A / @0-B0@ to @124-B0@" | Labels joined by "through" | The CSV column `Section` keeps the id form, so file text and subtitle are made separately |
| **Capacity** window context and compressed diagram title (`app/ResultViews.cpp:837` to `:840`, `:849` to `:852`) | "Section: route0 / A / @0-B0@ to @124-B0@" | Labels joined by "through" | Item tooltip of the chooser; the CSV column `Section` |
| **Capacity by area** context (`diagrams/AreaCapacityWindow.cpp:768` to `:771`, `governingText` at `:272` to `:285`) | "Area: ... Governing resource: @0-B0@" | Labels | The CSV column `Area` |
| Chart section strip (`diagrams/DiagramWindow.cpp:401`) | "1:@0-B0@" | "1: B0 0.000 to 0.230 km" | Hover text |
| Strip hover and notices (`:321`, `:331`; `diagrams/BlockingTimeDiagram.cpp:48` to `:54`, `:313`, `:420` to `:434`) | "Physical reference section 1:@0-B0@ \| Route: ..." | The label, then the id in parentheses | The same text |
| Hover of a blocking envelope (`app/ResultViews.cpp:129` to `:134`) | "Resource: @0-B0@" | "Resource: B0 0.000 to 0.230 km (@0-B0@)" | The same text |
| Validation messages (`scene/SceneValidator.cpp:257` to `:260`, `:312`, `:313`, `:343` to `:349`, `:381` to `:396`, `:1093` to `:1095`) | Ids | The id, then the label in parentheses; the parentheses keep the comma of a switch label apart from the commas of a list | The message and the **Path** column |
| Run review (`app/RunController.cpp:191` to `:193`) | "target=0-B0" | Unchanged | The text |
| Result tables of **Capacity** and **Capacity by area** (`app/ResultViews.cpp:768` to `:810`; `diagrams/AreaCapacityWindow.cpp:85` to `:90`, `:139`, `:231`, `:245`) | Ids | Unchanged | The cell; the label is its tooltip, because **Export capacity CSV...** and **Export area capacity CSV...** write the same rows |

The status text after a target is picked, "The target of incident ... is now ..." (`app/MainWindow.cpp:2912`), reads the text of the **Target** list and so shows the label without a change of its own.

## What does not change

- Scene files: block ids, route tokens, the references of restrictions, boundaries, dependencies and signals, and incident targets. The schema version stays 1 and no migration step is added.
- Legacy import and export: the importer forms the same ids and the exporter maps them back to legacy rows (`scene/SceneExporter.cpp:107` to `:175`).
- Recovery copies: they are bundles of the scene (`recovery/RecoveryStore.h:108`), and labels are not stored.
- Golden files: 127 of the 134 hold ids and none holds a text of the user interface, so none changes.
- CSV exports: same columns and keys. Blocking time `Block` (`diagrams/BlockingTimeDiagram.cpp:19`, `:391`), trajectory `Block` (`diagrams/RunExports.cpp:33`), capacity `Block`, `Section` and `Reference label` (`:455` to `:457`, `:508`, `:549`; `diagrams/CapacityScope.cpp:54`, `:227`) and area capacity resources (`diagrams/AreaCapacityWindow.cpp:470`, `:498`, `:526`). `BlockingTimes.txt` keeps its form.
- Live frame: `section_id` (`io/LiveFrame.cpp:54`, [frame schema](../live/frame-v1.schema.json)) keeps the id and gains no label field.
- Copy actions: **Copy canonical ID** and **Copy signal location** copy the same text; `tests/e2e/VisualPolish.cpp:1755` to `:1791` checks the second.
- Diagnostics: code, item id, related id, path and suggested fix keep the id, also in the JSON of `scene_tool` (`scene/SceneDiagnostic.cpp:30` to `:42`). Only message texts gain labels.

The exports gain no label column in this step. Scripts join the CSV rows on the id with scene files, `BlockingTimes.txt` and the live frame, and the [run output](../guides/run-output.md) page lists the columns. If a label column is wanted later, it goes at the end of each file, so that the column positions of the blocking time export (`diagrams/BlockingTimeDiagram.cpp:18` to `:25`) and the header checks of `tools/e2e/csv_export_smoke.sh`, which match the start of each header line, stay valid.

## Implementation plan

1. Add the label to `scene/SectionInventory.h` and `.cpp`, beside `formatSceneSectionCoordinate`:

   ```cpp
   /// What a person reads for a section: "B0 21.010 to 21.310 km" for a block section, or the piece on each
   /// track, "B0 21.010 to 21.235, B1 21.252 to 21.450 km", for a switch section.
   std::string sceneSectionLabel(const SceneSectionDescriptor& section);

   /// The labels of the sections of an inventory by runtime id and by block id. A label that two sections
   /// share ends with the id in parentheses; label() returns the reference itself when it names no section.
   struct SceneSectionLabels {
   	std::unordered_map<std::string, std::string> byReference;
   	std::string label(const std::string& reference) const;
   };
   SceneSectionLabels sceneSectionLabels(const SceneSectionInventory& inventory);
   ```

   Test it next to the inventory cases in `tests/test_scenebuilder.cpp`, on the `line` and `crossovers` fixtures: a block, a switch, a piece with equal ends, a shared label and an unknown reference.
2. Scene-time call sites, which already build an inventory:
   - `scene/SceneValidator.cpp`: the five messages listed above, from the inventory built at `:902`.
   - `editors/InfrastructurePanel.cpp:558` to `:566`: `sectionLabel` returns the shared label. `populateSectionCombo` (`:532` to `:556`) then serves the signal (`editors/infrastructure/SignalFacets.cpp:98` to `:103`), dependency (`BlockRouteFacets.cpp:265` to `:269`), restriction (`:283` to `:290`) and station boundary choices (`StationFacets.cpp:325` to `:340`), and the route catalog and list (`InfrastructurePanel.cpp:395` to `:419`).
   - `editors/IncidentPanel.cpp:425` to `:429`: label as text, id as data and tooltip.
   - Preview: `scene/TrackPreview.cpp:352`, where each preview signal is added, gives it its label; `graphics/NetworkPresenter.cpp:666`, `widgets/InspectorPanel.cpp:436` to `:455` and `app/MainWindow.cpp:3268` read it.
3. Run-time call sites:
   - `graphics/NetworkPresenter.cpp` builds the labels once when a run starts, from the scene of the run, and sets them on the signal items next to the section ids (`:1372` to `:1421`). `graphics/items/SignalItem.cpp:70` to `:77`, `NetworkPresenter.cpp:237` and `:262`, and `app/MainWindow.cpp:4359` use them.
   - `widgets/InspectorPanel.cpp:479`, `:525`, `:574`, `:616`, `:645`, `:646` and `:678` to `:698`: scene track ids from `sceneTrackId` and section labels, with tooltips for the runtime number and the id. Every field of the inspector sets its tooltip to its own text when the text changes (`:178` to `:183`), so a tooltip with the id is set after `setText`, or that hook changes.
   - `app/MainWindow.cpp:3406` to `:3431`: **Copy section ID** on the signal menu of a run.
4. Result windows:
   - `diagrams/CompletedRunData.h` and `diagrams/CompletedRunCapture.cpp:226` to `:253` keep the labels of the run's sections, from the scene that `captureCompletedRun` already receives.
   - `diagrams/RunExports.cpp:295` to `:305` and `diagrams/BlockingTimeScope.h` pass them to the choosers: `BlockingTimeScope.cpp:91` and `:92`, `CapacityScope.cpp:345` to `:352`, `AreaCapacityWindow.cpp:629`. The choosers keep selecting by index, as they do now (`BlockingTimeScope.cpp:143`, `AreaCapacityWindow.cpp:640`).
   - `widgets/ChoiceComboBox.h`: since #1076 the tooltip of the field shows the current text and then the tooltip of the field (the **Target** of an incident has its own). With labels it shows the current text, then the tooltip of the current item (the id) when it has one, then the tooltip of the field.
   - `diagrams/DiagramWindow.cpp:321`, `:331` and `:401`, `diagrams/BlockingTimeDiagram.cpp:48` to `:54`, `:313` and `:420` to `:434`, `app/ResultViews.cpp:129` to `:134` and `:524` to `:525`, `diagrams/CapacityScope.cpp:226` to `:227`: labels in the strip, hover texts, notes and subtitles. `capacityScopeLabelFrom` (`CapacityScope.cpp:226` and `:227`) also fills the `Section` column of the capacity CSV and, through `AreaCapacityWindow.cpp:374` to `:383`, the `Area` column of the area capacity CSV, so it keeps the id form and a separate display label serves the window context, the compressed diagram title and the area context (`app/ResultViews.cpp:837` to `:852`, `diagrams/AreaCapacityWindow.cpp:768` to `:771`).
   - The result tables keep the id and get the label as cell tooltip.
5. Update the tests listed under the options, then the glossary: a short entry that explains the track id, the block id, the block section id, the switch section id with its separator `-`, the label, and the runtime track number. Review the manual pages that show ids (`docs/guides/capacity-analysis.md:74` to `:79`, `signalling-levels.md`, `scene-schema.md` and `tutorial-minimal-scene.md:46`) once the code is final.

Verification for the code change: build, CTest, `tools/e2e/visual_polish_smoke.sh`, `tools/e2e/headless_smoke.py` and `tools/e2e/roundtrip_smoke.py`, and a check of the inspector, the **Target** list, the three choosers and the strip on Paimpol and Netherlands in the application.

## Where part 1 differs from the plan

Part 1 of the change made steps 1 and 4, step 2 without the preview, and the documentation of step 5. It differs from the plan in these points:

- `SceneSectionLabels` is in its own header, `scene/SectionLabels.h`, which `scene/SectionInventory.h` includes. The result windows include it after Qt headers, and Qt's `signals` macro breaks `scene/SceneModel.h`, which has a member of that name.
- `SceneSectionLabels::labelWithId` gives the label with the id in parentheses, "B0 0.000 to 0.230 km (@0-B0@)", for the strip hover, the notes and the envelope hover. A label that already ends with the id, by rule 5, keeps it once.
- The **Target** list of a signal failure names an authored signal by its id and the label of its protected section, "sig1 (T1 0.000 to 1.000 km)", as rule 7 says. A signal without a protected section shows its id.
- Where labels are listed together, in the note on ambiguous reference sections, the note on sections without a usable extent and the governing resources of **Capacity by area**, they are joined with semicolons, since a switch label holds a comma. The id forms keep their commas.
- The tooltip of a choice field leaves out a line that repeats an earlier one, so a field whose item and field tooltips are equal shows that text once.

## Part 2

Part 2 made the preview item of step 2 and all of step 3. On Paimpol:

- A signal of the preview keeps the label of its section and its name by rule 7 (`TrackPreviewSignal` in `scene/TrackPreview.h`). The first signal of B0 has the inspector title "Preview: Signal B0 0.230 to 0.310 km (not running)". **Protected section** and **ID of Block Section** show "B0 0.230 to 0.310 km", their tooltips add "Section ID: @1-B0@", and the details keep "Track B0; section @1-B0@; forward; authored signal IDs: None". The tooltip of the head starts with "Track B0 at 0.23 km; section B0 0.230 to 0.310 km", and its context menu is called "Preview: B0 0.230 to 0.310 km".
- `showRuntime` builds the labels from the scene of the run and gives each signal head the scene track id and the labels of its sections (`graphics/items/SignalItem.h`). The tooltip of the forward head at 0.23 km starts with "Track B0 at 0.23 km; ahead B0 0.230 to 0.310 km; behind B0 0.000 to 0.230 km".
- **Signalling Info**, Node Info, Station Node Info, Arc Info and Connection Info show scene track ids and labels. A field that shows one has the text as its tooltip and, below it, "Runtime track number: 0" or "Section ID: @1-B0@". **Connected signals** of an arc joins its labels with semicolons and lists the ids in the tooltip.
- **Find** lists a signal with the labels and finds it by the ids of its sections too, which it does not show. It names the track of a station node by its scene id: "Station: Brelidy-Plouec (track B0)".
- The signal menu of a run has **Copy section ID**. It copies the id of the section ahead of the head, or of the section behind it when there is none ahead: the section that **Protected section** shows.

Where part 2 differs from the plan:

- The examples in [The problem](#the-problem) and in the table name the signal at 0.23 km by `@0-B0@`. A signal of the preview stands at the start of the section it names, and the first section of a track has no signal, so that signal names `@1-B0@`. In a run the reversed head at 0.23 km has `@0-B0@` ahead and `@1-B0@` behind, not "behind none".
- The table keeps the id of a signal only in the inspector. **Find** also matches the ids, so a search for `@1-B0@` still finds the heads of that section.

## Open questions

- Station context for route choosers. The stations of a route are known in travel order (`sceneRouteStations`, `scene/SectionInventory.h:194`), so a chooser could show "Guingamp to Gourland" beside the label of each block of a route. In Netherlands, Amsterdam_Hilversum_Student and Assignment_Gvc_Gdg_Ut the station names are codes.
- Whether the runtime track number should follow the digits of the track id instead of its sorted place. That changes no label but would make **Track ID** right without a tooltip; it touches the run and needs its own issue.
- Authored labels or track names. The scene format has no field for them.
- A label column in the exports, as described above.
