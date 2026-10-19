# Documentation decision

How the documentation is structured, written, built, checked and shipped, and the options that were compared. The place of every existing page is in the [page map](documentation-page-map.md). Decided in #915 on 2026-10-19.

## Decision

| Question | Choice |
| --- | --- |
| Structure | Two sections split by audience: the manual (`docs/guides/`) for users and the developer docs (`docs/development/`, `docs/architecture/`). The page kinds of Diataxis (tutorial, how-to, reference, explanation) describe a page inside a section; they are not folders. |
| Site | Sphinx with MyST-Parser, so the sources stay Markdown, and the Furo theme. |
| C++ reference | Doxygen, built into the same site under `api/`. |
| Manual in the app | The same Markdown pages of `docs/guides/`, built a second time with Sphinx's `qthelp` builder and Qt's `qhelpgenerator` into a `.qch` file that the app shows with `QHelpEngine`. |
| Versions | One copy of the site per release tag, built by the publish job. No versioning plugin. |
| Screenshots | Produced by a test program in offscreen mode and listed in a manifest that `check_docs.py` reads. |
| Checks | `tools/docs/check_docs.py` stays the gate, together with `sphinx-build -W`. |

## Requirements

From the issue and the owner's comments:

1. Sources are Markdown in `docs/` and comments in the code. Links are relative and no GitHub-only Markdown feature is used.
2. The built site is static HTML that any web host serves and a browser opens from disk.
3. One command builds the whole site (guides and C++ reference) into the build folder, and one command serves a live preview.
4. Tool versions are pinned in a file in the repository.
5. The same manual is used offline in the app (#917) without writing the text twice.
6. The maintenance status of each tool is checked before choosing.
7. The site has a developer section and a user section, and keeps one copy per release.

## Options compared

Release dates and versions were read on 2026-10-19 from GitHub and PyPI.

| Option | Search | Copy per release | Offline and in the app | Link checking | Maintenance | Effort |
| --- | --- | --- | --- | --- | --- | --- |
| Plain Markdown on GitHub | GitHub code search only | A Git tag shows the docs of that tag | No HTML, so nothing to show in the app | `check_docs.py` | Not applicable | None, but no manual in the app and no C++ reference |
| MkDocs with Material | Built in | `mike` plugin | Offline plugin; the theme needs JavaScript, which Qt's text widgets do not run; no `.qch` | Validation settings | MkDocs 1.6.1 on 2024-08-30 is the last release. Material 9.7.7 on 2026-07-17, maintenance only | Low, plus a second pipeline for the app |
| Zensical (successor of Material) | Built in | Not built in; a fork of `mike`, described as transitional | Offline plugin; no `.qch` | Reported broken anchors on the current pages | 0.0.71 on 2026-10-16, before its first 0.1 release | Low, but the tool still changes |
| Sphinx with MyST | Built in | Build each tag into its own folder | `qthelp` builder is part of Sphinx; `qhelpgenerator` comes with Qt | Warnings for missing pages and anchors; `-W` turns them into errors | Sphinx 9.1.0 on 2025-12-31, MyST-Parser 5.1.0 on 2026-05-13, both active | Low to medium |
| Docusaurus | Built in through a plugin or a hosted service | Copies the docs into `versioned_docs/` for each version | No `.qch` | Broken link check at build | 3.10.2 on 2026-07-10, active | Medium; adds Node and React to a C++ and Python repository |
| Doxygen alone | Built in | One output folder per release | `GENERATE_QHP` writes Qt Help input | Warnings for unresolved references | 1.18.0 on 2026-08-13, active | Low for code; the page tree follows folders and ordering 45 pages of prose needs Doxygen commands in the Markdown |

Notes:

- MkDocs and Material. The Material team has stopped feature work and calls MkDocs unmaintained since August 2024 ([announcement](https://squidfunk.github.io/mkdocs-material/blog/2025/11/05/zensical/)). The announcement ends critical fixes on 2027-05-05; the Material README gives 2026-11-05. Either date is too near for a new site.
- Zensical. It reads `mkdocs.yml` and builds Material projects ([compatibility](https://zensical.org/docs/compatibility/mkdocs/)). Versioning is listed on its [roadmap](https://zensical.org/roadmap/) as planned. A new site would depend on features that do not exist yet.
- Docusaurus. A version is a copy of the docs inside the repository ([versioning](https://docusaurus.io/docs/versioning)), so old copies would need the same reviews as current pages.
- Doxygen alone stays the fallback if Sphinx proves too heavy.

## Why Sphinx with MyST

It is the only option that gives the site and the app's manual from one Markdown source. Measured on 2026-10-19 on macOS with Sphinx 9.1.0, MyST-Parser 5.1.0, Furo 2025.12.19 and Qt 5.15.19, using the `docs/` folder unchanged:

- The site built in about 6 seconds with 54 warnings: 47 pages not yet in a table of contents and 7 links to files outside `docs/`. With `myst_heading_anchors = 4` no heading anchor was missing.
- `sphinx-build -b qthelp` and `qhelpgenerator` made a 3.9 MB `.qch` from all of `docs/`. A small Qt program opened it with `QHelpEngine` and showed a page in a `QTextBrowser` with headings, lists, bold text, links and a PNG screenshot. Tables, the contents tree and search were not checked.
- The `qthelp` builder uses a plain theme, so the site can use Furo while the app gets simple HTML. Qt's text widgets support a subset of HTML 4 and CSS 2.1 ([Qt](https://doc.qt.io/qt-5/richtext-html-subset.html)).
- A build of the manual alone, with the developer folders excluded through a tag in `conf.py`, warns on every link from a manual page to a developer page. This keeps the manual self-contained.
- Zensical 0.0.71 built the same pages in about 2 seconds with 5 link issues.

The cost is four pinned Python packages and Doxygen, against none for plain Markdown.

## Hosting

- The sources are Markdown in `docs/` and comments in the code. The output is static HTML with relative links that opens from disk.
- Only the publish job depends on GitHub Pages. It builds each release tag into `<version>/`, keeps `latest/`, and writes a root page that lists the versions.
- Moving to another host needs a new publish step and, for each old path, a stub page that redirects with a meta refresh.
- The app's `.qch` is built from the same tag, so the manual in the app matches its version (`EGTRAIN_VERSION` in `CMakeLists.txt`).
- If Qt Help cannot be packaged on a platform, #917 falls back to opening the shipped HTML in the system browser. That loses the in-app contents and search.

## Local build and preview

#918 adds both commands:

```bash
cmake --build build --target docs
cmake --build build --target docs-preview
```

`docs` runs Doxygen and Sphinx into `build/docs/`; open `build/docs/index.html`. `docs-preview` runs `sphinx-autobuild`, which serves the site on a local port and rebuilds a page when its Markdown changes. The C++ reference is refreshed by `docs`.

The site keeps its table of contents in `docs/index.md`, the only Sphinx directive in the sources. `docs/README.md` stays a short landing page for readers on GitHub.

## Pinned versions

| Tool | Version | Pinned in |
| --- | --- | --- |
| Sphinx | 9.1.0 | `docs/requirements.txt` |
| MyST-Parser | 5.1.0 | `docs/requirements.txt` |
| Furo | 2025.12.19 | `docs/requirements.txt` |
| sphinx-autobuild | 2025.08.25 | `docs/requirements.txt` |
| Python | 3.12 or newer, which Sphinx 9.1.0 needs | `docs/requirements.txt` comment |
| Doxygen | 1.18.0 | version check in the `docs` target |
| `qhelpgenerator` | the Qt 5 of the build | already a build requirement |

## Screenshots

What exists. The offscreen smokes already save the main window and some dialogs with `grab()` (`tests/e2e/VisualPolish.cpp`, `tests/e2e/Dialogs.cpp`). No script makes the 12 `docs/images/guide-*.png` files or the two README images, and the window sizes differ. The application has no dark theme.

Process:

1. A test program in `EGTRAIN/QEGTRAIN/tests/e2e/` opens a named case, puts the window in a named state and saves each picture with `grab()`.
2. `tools/docs/capture_screenshots.sh` runs it with `QT_QPA_PLATFORM=offscreen` on macOS arm64 and writes `docs/images/` and `docs/images/captures.json`. Running it again regenerates every capture.
3. `captures.json` lists, for each file, its case, state, size and the application version. A picture that cannot be scripted is marked manual there, with the steps to take it.
4. Names are `<page>-<subject>.png`, lower case with hyphens, for example `getting-started-open-a-case.png`.
5. Window captures are 1200 by 750 pixels at a device pixel ratio of 1. Dialogs keep their natural size. Nothing is wider than 1200 pixels.
6. Alt text says what the picture shows and in which state.
7. Light only, until the application has a dark theme.
8. Dated comparison pictures under `docs/ui/images/` are records and are not regenerated.

A stale picture is found in two ways:

- `check_docs.py` fails when a PNG is not in `captures.json`, an entry has no file, a size differs from the manifest, or the recorded version differs from `EGTRAIN_VERSION` in its major or minor part.
- `capture_screenshots.sh --check` captures again into a temporary folder and lists pictures where more than 2 percent of the pixels differ. It runs from the release checklist, not on every pull request, because fonts differ between systems.

## Onboarding path

A new developer reads, in this order:

1. `README.md`, then the developer section of the site.
2. [Getting started as a developer](getting-started.md): tools, clone, configure, build and run. A first full build with tests took about 14 minutes on an Apple silicon laptop and about 7 minutes in a Linux container with 10 CPUs.
3. The first change in the same page: one test assertion, then the quick unit tests.
4. [Source layout](../architecture/source-layout.md), then the [scene model](../architecture/scene-model.md), then the C++ reference of the folder being changed.
5. The build and test pages, the [coding guidelines](coding-guidelines.md) and the checks a pull request must pass.

The target is a first passing change on a clean machine within one hour. #916 measures it on one platform and writes the time in the getting started page.

## Writing guide

Voice and form:

- Write to the reader as "you", in the present tense. One action per step, in the order the reader does it.
- Plain English and short sentences. Sentence-case headings. No em or en dashes and no marketing words.
- Start each page with one sentence on what the reader gets from it.
- The manual describes what the current release does. Dated test notes go to the records, and features that are not built are left out.

Terms:

- Use the words of the [glossary](../guides/glossary.md). A case study is what the user opens, edits and runs. Say scene only where the application does, as in **File > Open Scene Folder...**, and for the data files.
- Add a new term to the glossary before using it, and link its first use on a page.

Units:

- Write the unit after every number: 9000 s, 30 km/h, 1.5 m. Times of day are `HH:MM:SS`.
- Say what a time counts from. Planned times are elapsed seconds from the base time of the case.
- Field labels keep the unit the application shows, as in **Minimum dwell (s)**.

Menus and controls:

- Bold only names that exist in the application, spelled as shown, with its capitals and ellipsis: **File > Open Case Study...**. Join menu levels with ` > `.
- Name the kind of control the first time: the dock **Run Results**, the dialog **Run simulation**, the button **Export CSV...**.
- Use code format for file names, keys in files, commands and paths. Shortcuts read Ctrl+R; a page that lists shortcuts says once that Ctrl is Command on macOS.
- Do not use bold for emphasis, so every bold span can be checked against the application.

## What check_docs.py checks

It already checks relative links, heading anchors, empty alt text, orphan images, pages that `docs/README.md` does not reach, `bash`, `sh` and `json` examples, dashes and local paths. #916 adds:

1. `github-only`: GitHub alerts (`> [!NOTE]`), `<details>`, links that start with `/`, and links to `github.com/Ancientkingg/EGTRAIN/blob/` for files in the repository.
2. `heading`: one `#` heading, first in the file, and no skipped levels.
3. `manual-link`: a page in `docs/guides/` links only to pages in `docs/guides/`.
4. `image`: the name pattern, the width and the `captures.json` rules above.
5. `ui-name`: every bold span in `docs/guides/` is in a list of menu paths, dock, dialog, button and field names that a test writes from the running application. The list is a build output, so the check is skipped without it and runs in the CI job that builds the application.

Each new check gets one case in `tools/docs/test_check_docs.py`. Prose style is not checked by machine beyond the dash rule.

## Follow-up

- #918: `docs/conf.py`, `docs/requirements.txt`, `docs/index.md`, the Doxygen configuration, the `docs` and `docs-preview` targets, a CI job that builds the site on changes under `docs/`, and the publish job.
- #916: the new checks, the moves of the page map, the capture program, then a review of each manual and developer page against the application.
- #917: the manual-only build, the `.qch` target, the help window with contents and search, the `.qch` in each package, and context help.

## Open questions

- How a Markdown page links to a generated C++ reference page. MyST does not resolve `../api/classFoo.html` as a document. To settle in #918.
- Whether Sphinx search works from `file://`. The index is a script, but result summaries are fetched, which some browsers block from disk. To settle in #918.
- Whether QtHelp, `qhelpgenerator` and Qt's SQLite plugin are in the Windows Qt install of CI (it installs only `qtcharts` as an extra module) and in the AppImage. To settle in #917.
- Furo's last release is from 2025-12-19 and sphinx-autobuild's from 2025-08-25. Both can be replaced: Sphinx's own `alabaster` theme, and a plain web server.
