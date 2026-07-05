# Release Testing Checklist

Use this checklist before merging release workflow changes or pushing a `v*` tag.

## CI Run

- Confirm the `Release` workflow finishes with `Package macOS`, `Package Windows`, and `Package Linux` green.
- Confirm `Publish GitHub Release` is skipped on branch pushes.
- Download these artifacts from the run:
  - `QEGTRAIN-macos-arm64`
  - `QEGTRAIN-windows-x64`
  - `QEGTRAIN-linux-x86_64`

## Artifact Checks

- macOS: unzip `QEGTRAIN-macos-arm64.zip`; confirm it contains `QEGTRAIN.app`.
- Windows: unzip `QEGTRAIN-windows-x64.zip`; confirm it contains `QEGTRAIN.exe`, Qt DLLs, `platforms/qwindows.dll`, a `libzmq` DLL, and `Input/`.
- Linux: mark `QEGTRAIN-linux-x86_64.AppImage` executable; confirm `--appimage-extract` creates `squashfs-root/usr/bin/QEGTRAIN` and `squashfs-root/usr/bin/Input/`.

## Tagged Release Rehearsal

- Push a pre-release tag such as `v1.0.0-rc1`.
- Confirm the release job publishes all three platform artifacts.
- Confirm GitHub marks the tag as a pre-release.
- Delete the test release and tag after the rehearsal if it was only for CI validation.
