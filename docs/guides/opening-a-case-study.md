# Opening an `.egscene` case study

An `.egscene` file contains one canonical case study. It is a ZIP archive with
the same JSON data that EGTRAIN loads from a V1 scene directory.

## Open and run

1. Download the case-study `.egscene` file from the
   [EGTRAIN releases page](https://github.com/Ancientkingg/EGTRAIN/releases).
2. In EGTRAIN, choose **File > Open Case Study...** and select the file.
3. Review any validation diagnostics shown by the application.
4. Choose **Run Scene** when the case study is ready.

Opening a bundle does not run it or modify the downloaded file. Use
**File > Save Case Study As...** to create a new `.egscene` after editing the scene.

If a scene is older and a reviewed migration is available, EGTRAIN offers
**Upgrade a Copy...** and leaves the original unchanged. If no migration is
registered, it reports that the scene is unsupported. A newer schema or bundle
requires a newer EGTRAIN release; the dialog can use **Check for Updates...**.

If EGTRAIN rejects a downloaded file, keep the diagnostic message and download
the file again. The reader rejects truncated archives, unknown files, unsafe
paths, duplicate entries, and files that exceed the documented bundle limits.

## Instructor and command-line use

Directory scenes remain the editable source format. The command-line tool can
pack, inspect, and unpack them:

```bash
scene_tool pack path/to/scene case-study.egscene
scene_tool validate case-study.egscene
scene_tool unpack case-study.egscene path/to/unpacked-scene
```

The unpack destination must be a new path; the tool does not replace an
existing file or directory.

The simulator also accepts a bundle directly:

```bash
QEGTRAIN --scene case-study.egscene -g 0 -TSM 0 -RC 0
```

See the [bundle specification](../architecture/scene-bundle.md) for the ZIP
layout, version fields, entry limits, and safety rules.
