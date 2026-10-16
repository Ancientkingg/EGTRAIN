# Code signing

This document covers Authenticode signing of the Windows program files and the Windows installer, and the two modes of
the macOS release (see [macOS releases](#macos-releases)). It records state, ownership and checks. It states no decision
of any signing service: every fact that only the owner can give is a field that reads Not recorded.

## Current state

- No Windows file is signed. The package workflow has two jobs that sign the three program files and then the installer
  through SignPath, and both are off until the owner sets them up (see [Signing in CI](#signing-in-ci)). Every run of
  the package workflow reports the signature state of the program files with `tools/release/verify_signatures.ps1` and
  fails for no file. Without the setup, the workflows use no credential other than the token that GitHub gives to each
  run.
- The Windows downloads are the installer `EGTRAIN-Setup-<version>-x64.exe` and the portable ZIP
  `EGTRAIN-Portable-<version>-x64.zip`.
- The macOS app is signed by `tools/release/sign_macos.sh` in the package workflow. The signature is ad hoc unless the
  environment `macos-signing` holds a Developer ID certificate (see the
  [macOS signing script](build-and-test.md#macos-signing-script)). An ad-hoc signature carries no publisher identity.
  With notarization credentials in the same environment, `tools/release/notarize_macos.sh` also notarizes and staples the
  app and the disk image (see the
  [macOS notarization script](build-and-test.md#macos-notarization-script)); without them nothing is notarized. The two
  modes, the settings that the owner adds and what a user sees at the first start are in
  [macOS releases](#macos-releases). The Linux AppImage is not signed.
- The in-app update checks the SHA-256 and the size of the downloaded package against `update-manifest.json`, an asset
  of the same release. On Windows it checks no signature, so these checks detect a damaged download and do not show who
  published the package. On macOS the staged app is also checked with `codesign --verify`, which an ad-hoc signature
  passes.
- A release page does not say whether the macOS app is notarized or whether the Windows files are signed. The release
  text, built by the Prepare release step of the release job in `.github/workflows/release.yml`, says nothing about
  signing. [Release text](#release-text) has the lines for macOS.

## What would be signed

The files are `QEGTRAIN.exe`, `egtrain_update_helper.exe`, `scene_tool.exe` and the Windows installer. The ZIP and the
installer must contain the same signed program files, so signing happens before the ZIP is built and before the
installer is compiled. The Windows finalize job writes the ZIP and compiles the installer from the directory that the
Windows package job uploads, so that directory must already hold the signed files. The signing job does this: it
replaces the uploaded directory with one that holds the signed files. The installer can be signed only once it is
compiled, so a second job signs it and replaces the uploaded installer. The release job takes the SHA-256 and the size
of each file it publishes for `update-manifest.json`. A ZIP file carries no Authenticode signature of its own.
Signatures use SHA-256 and a trusted timestamp where the signing service supports them.

This project does not sign the Qt, ZeroMQ and other runtime libraries and plugins in the package; they keep whatever
signature they have, which may be none. The scenes, the guide and the `.egscene` case studies are data and are never
signed.

## Choosing a signing path

The first choice is the SignPath Foundation free programme for open-source projects, if the project meets its
conditions.

The fallback is a paid service or certificate. It is chosen only after the reason the free path does not work is written
in the fallback record below, and then it is the lowest-cost option that works with GitHub Actions.

### Eligibility record

The programme publishes its own conditions. Whoever applies reads the current published conditions, records the link and
the date, and fills the table. Not recorded means that nothing has been recorded.

| Item | Record |
| --- | --- |
| Repository | Public, owned by the GitHub user account Ancientkingg, not a fork (from `gh repo view`) |
| Built where | GitHub Actions workflows in the repository |
| Licence in the repository | `LICENSE` is the GNU General Public License version 3, and the README names it as GPL-3.0-only for the source code |
| Owner confirms that this is the intended OSI-approved licence | Not recorded |
| `LICENSE` in the commits that a release is built from (the release workflow builds pushes to `production` and `v*` tags). Check: `git fetch`, then `git ls-tree origin/production LICENSE` prints one line when the file is there and nothing when it is not | Not recorded |
| Third-party components in the Windows package and their licences | Not recorded |
| Licence of the case-study data (the README says the code licence does not cover the case-study datasets; that is a separate decision) | Not recorded |
| Link to the programme's published conditions and the date they were read | Not recorded |
| Application: date, submitted by, link to the application or reply | Not recorded |
| Result (accepted, declined, pending) and date | Not recorded |
| Project and signing policy names as issued | Not recorded |
| Signer name exactly as issued, to be shown in the certificate subject | Not recorded |
| Certificate issuer name as shown on a signed file | Not recorded |

The signer name is whatever the service issues. Documents and release text must not say or imply that it identifies an
individual developer or the project when it does not. The version information of `QEGTRAIN.exe` names TU Delft as
company (`CompanyName` in `EGTRAIN/QEGTRAIN/resources/app/egtrain.rc.in`). That text is not the signer, and the owner
decides separately whether it changes.

### Fallback record

Fill this before any paid option is chosen.

| Item | Record |
| --- | --- |
| Why the free path is unavailable or unsuitable | Not recorded |
| Date | Not recorded |
| Options considered and their cost | Not recorded |
| Decision taken, and by whom | Not recorded |
| Who pays and who holds the account | Not recorded |

## Ownership

| Role | Held by | Responsible for |
| --- | --- | --- |
| Signing owner (the repository owner) | Not recorded | Applies to the programme or buys the service, accepts its terms, receives its mail, creates and maintains the protected environment and its credentials in the repository settings, and answers any approval step the service requires |
| Second contact with access | Not recorded | Takes over if the owner is not available |
| Maintainers | Anyone who changes the workflows | Change the workflows and the signature check; never handle credentials |
| Reviewers | Anyone who reviews such a change | Check that no change exposes a credential |

Nobody but the signing owner and the second contact may hold the credentials.

## Repository authorization

These rules apply to any signing that is set up for this repository.

- Credentials exist only in the GitHub environments `windows-signing` and `macos-signing`, which only the `production`
  branch and `v*` tags may use. They are never in the repository, in workflow logs, in workflow artifacts or in release
  assets. No certificate, private key, password or token is committed.
- Signing runs only for pushes to `production` and for `v*` tags. Pull requests, including pull requests from forks and
  the pull request from `main` to `production`, and manual runs never sign and never need credentials. Ordinary
  development builds and the checks of pull requests stay unsigned and fully testable.
- A signing step is skipped, not failed, when nothing is configured, so the portable ZIP and the installer stay usable
  unsigned and the macOS app stays ad hoc. A macOS certificate or notarization group that is only partly configured
  fails the job instead (see [What the owner adds](#what-the-owner-adds)).
- When signing is configured, the release fails if a required file is unsigned, invalid or signed by an unexpected
  signer, and the ZIP and the installer are built from the files that were checked.

## Signing in CI

The `sign-windows` job of `.github/workflows/package.yml` signs `QEGTRAIN.exe`, `egtrain_update_helper.exe` and
`scene_tool.exe` through the SignPath action `signpath/github-action-submit-signing-request`. It runs only for a push
to `production` or a `v*` tag, and only when the repository variable `WINDOWS_SIGNING` is `signpath`. In every other run
it is skipped, and the Windows finalize job builds the installer and the ZIP from the unsigned files.

The job downloads the payload artifact, uploads the three program files alone as the artifact
`QEGTRAIN-windows-x64-unsigned` (so no Qt or other library can be signed), submits the signing request for that
artifact, copies the three signed files into the payload, runs `verify_signatures.ps1 -Require` on the payload and
replaces the artifact `QEGTRAIN-windows-x64-payload` with it. The finalize job and the release job read that artifact.
When the request is rejected or times out, or a check fails, the job fails and the finalize job does not run, so no
installer or ZIP is built from unsigned files. A re-run of the failed job replaces the artifacts of the first attempt.

The `sign-windows-installer` job runs after the finalize job, and only when `sign-windows` succeeded, so every run that
did not sign the program files skips it and keeps the unsigned installer. It downloads the installer that the finalize
job compiled from the signed files and uploaded unsigned, uploads that file alone as the artifact
`EGTRAIN-windows-x64-installer-unsigned`, and submits a second signing request for it. It then runs
`verify_signatures.ps1 -Require` on the signed installer and on the three program files of the payload, and replaces the
artifact `EGTRAIN-windows-x64-installer` with the signed installer only after both checks passed. When the request or a
check fails, the job fails, and the release job, which waits for the whole package workflow, does not run. The installer
smoke of the finalize job installs the installer before it is signed, and nothing in CI installs the signed file. Only the
compiled installer is signed here; whether the uninstaller that Setup writes carries a signature has not been checked.

SignPath Foundation needs a person to approve each request, so a signed release needs two approvals, one for the program
files and one for the installer, and each signing step waits up to one hour for its approval.

The names below are the placeholders that the workflow reads. The owner creates them in the repository settings and in
SignPath. The SignPath documentation (https://docs.signpath.io/trusted-build-systems/github) describes the inputs. It
asks for the predefined GitHub.com trusted build system to be added to the SignPath organization and linked to the
project, and for the SignPath GitHub App to be installed when audit log checks are used.

| Name | Kind | Value |
| --- | --- | --- |
| `WINDOWS_SIGNING` | Repository variable | `signpath` turns the job on. Anything else, or nothing, leaves it off |
| `SIGNPATH_ORGANIZATION_ID` | Repository variable | The organization id that SignPath issues |
| `SIGNPATH_PROJECT_SLUG` | Repository variable | The project slug that SignPath issues |
| `SIGNPATH_SIGNING_POLICY_SLUG` | Repository variable | The slug of the release signing policy |
| `SIGNPATH_ARTIFACT_CONFIGURATION_SLUG` | Repository variable | The slug of the artifact configuration of the program files below. It must be set |
| `SIGNPATH_INSTALLER_ARTIFACT_CONFIGURATION_SLUG` | Repository variable | The slug of the artifact configuration of the installer below. It must be set |
| `WINDOWS_SIGNING_SUBJECT` | Repository variable | The subject of the signing certificate as `Get-AuthenticodeSignature` prints it. The check fails for any other signer |
| `SIGNPATH_API_TOKEN` | Secret of the environment `windows-signing` | The API token of a SignPath user who may submit requests for the policy |
| `windows-signing` | Environment | Deployment branches and tags limited to `production` and `v*` |

A called workflow receives only the secrets that its caller passes, so the release workflow passes the token to
`package.yml` by name (`secrets: SIGNPATH_API_TOKEN: ${{ secrets.SIGNPATH_API_TOKEN }}`) and passes no other secret. The
package check passes none, so it never holds the token. The jobs `sign-windows` and `sign-windows-installer` set the
environment, so they read the value stored in the environment `windows-signing`, and the token needs no other place.

The signing action reads the run and downloads the uploaded files with the token of its job, which its documentation says
needs `actions: read` and `contents: read`. Each of the two jobs therefore has its own `permissions` block with those two
read permissions. A called workflow cannot hold more than its caller grants, so the `package` job of the release workflow
and the `package` job of the package check have the same block. No workflow has a write permission except the release
job (`contents: write`).

The artifact configuration of the program files in SignPath lists exactly the three program files at the top of the
artifact, which GitHub uploads as a ZIP file:

```xml
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file-set>
      <include path="QEGTRAIN.exe"/>
      <include path="egtrain_update_helper.exe"/>
      <include path="scene_tool.exe"/>
      <for-each>
        <authenticode-sign/>
      </for-each>
    </pe-file-set>
  </zip-file>
</artifact-configuration>
```

The artifact configuration of the installer lists the one file, whose name `kAssetPatterns` in
`EGTRAIN/QEGTRAIN/update/ReleaseInfo.cpp` fixes:

```xml
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file-set>
      <include path="EGTRAIN-Setup-*-x64.exe"/>
      <for-each>
        <authenticode-sign/>
      </for-each>
    </pe-file-set>
  </zip-file>
</artifact-configuration>
```

`tools/release/verify_signatures.ps1` checks the signatures. Its usage and the rules for a file to pass are in
[Signature check](build-and-test.md#signature-check). The installer is checked by naming it with `-Name` in the
directory that holds it. Every run of the package workflow uses `-Report` on the unsigned package. The job
`sign-windows` uses `-Require` on the payload, and the job `sign-windows-installer` uses `-Require` on the signed
installer and again on the payload.

`test_verify_signatures` runs on Windows only. It creates a self-signed code-signing certificate in the certificate
store of the current user, signs copies of `scene_tool.exe` with it and expects the check to pass for a SHA-256
signature and to fail for an unsigned copy, a SHA-1 signature, another signer, a missing file, an untrusted chain and a
missing timestamp. The certificate is never exported or trusted, and it is removed when the test ends. The test runs in
the Windows leg of the pull request checks. `tools/e2e/test_ci_workflow.py` reads `package.yml` and pins the order of the
signing: the finalize job reads the payload that `sign-windows` replaced and signs nothing, the installer job runs only
after a successful `sign-windows`, checks the installer and the program files before it replaces the installer artifact,
and the release job runs only after the whole package workflow succeeded. The signing requests themselves have not run
against a real SignPath project, and a pull request run shows the signing jobs as skipped.

## Renewal and maintenance

| Item | Record |
| --- | --- |
| Validity period of the certificate or of the service agreement | Not recorded |
| Who watches the expiry, and how | Not recorded |
| Rotation of the access token of the signing service (interval, last done, next due) | Not recorded |
| What to update when the signer name or the service changes (the repository variable `WINDOWS_SIGNING_SUBJECT`, which holds the expected signer name used by the check, and the SignPath names in the table above) | Not recorded |

If no signing is configured when a release is due, the release is published unsigned and its release text says so. No
other certificate is used unless the fallback record is filled first.

## Checking a download

These checks need a Windows machine with PowerShell. Unpack the ZIP first for the three program files, and check the
installer as downloaded. Run the commands for each of `QEGTRAIN.exe`, `egtrain_update_helper.exe`, `scene_tool.exe` and
the installer, with the file name changed.

```powershell
$sig = Get-AuthenticodeSignature -FilePath .\QEGTRAIN.exe
$sig.Status
$sig.StatusMessage
$sig.SignerCertificate.Subject
$sig.SignerCertificate.Issuer
$sig.TimeStamperCertificate.Subject
```

For a signed release, `Status` is `Valid`, the `Subject` shows the signer name and the `Issuer` shows the issuer name of
the eligibility record (both rows read Not recorded until the owner fills them), and the time stamper line is not empty.
`StatusMessage` gives the status in words. For an unsigned file, `Status` is `NotSigned` and the three certificate lines
are empty. Any other status means the file must not be trusted.

In a shell where `signtool` is available (it comes with the Windows SDK), run `signtool verify /pa /v QEGTRAIN.exe`; use
the same command for each file. The output shows the signing chain and whether the signature is timestamped. An exit
code of 0 means that it verified. Compare the Issued to and Issued by lines of the signing certificate with the record
and ignore the other lines.

`Valid` means that the file is unchanged since it was signed and that the chain is trusted on this machine. It does not
mean that the program is free of defects or that it matches a given source revision. The checks apply once a release is
signed. Until signing is set up and a release is built with it, the Windows downloads show `NotSigned`.

`tools/release/verify_signatures.ps1 -Directory DIR -Report` prints the same findings for the three program files of an
unpacked ZIP, together with the digest algorithm and the timestamp (see [Signing in CI](#signing-in-ci)).

## Unsigned downloads

Until a release is signed, Windows may warn before it starts a program that was downloaded from the internet and has no
publisher signature (the SmartScreen screen "Windows protected your PC"). The wording and the options depend on the
Windows version. The project has not recorded how the screen looks on a clean machine: the release rehearsal records no
Windows desktop check.

A valid signature shows who signed a file. Whether Windows still warns for a signed program depends on the Windows
version and on checks of its own that the project has not observed. This document makes no promise either way. When a
release is signed, its release text must say which Windows files are signed.

## macOS releases

The macOS download is the disk image `EGTRAIN-<version>-macOS-arm64.dmg`. The package workflow builds it in one of two
modes. A push to `production` or a `v*` tag runs the macOS job in the environment `macos-signing`, and what that
environment holds decides the mode. Every other run, including every pull request, has no environment and builds the ad
hoc mode, so a pull request or a development build needs no credential. A paid Apple Developer membership is not needed
to release in the ad hoc mode.

| | Ad hoc, not notarized | Developer ID, notarized |
| --- | --- | --- |
| What `macos-signing` holds | None of the settings in [What the owner adds](#what-the-owner-adds) | The signing certificate and notarization credentials |
| Signature of the app | Ad hoc (`Signature=adhoc`). It keeps the app runnable on Apple silicon and names no developer. | Every nested file, then the app, signed with the Developer ID identity, the hardened runtime and a secure timestamp |
| Disk image | Not signed | Signed with the same identity |
| Notarization | None | Apple notarizes the app and then the image, and the ticket is stapled to both |
| First start | macOS refuses it until the owner of the Mac allows it in System Settings ([details](#first-start-of-an-ad-hoc-build)) | The confirmation that macOS asks for an app that was downloaded, and no other dialog ([details](#first-start-of-a-notarized-build)) |

Set the certificate and the notarization credentials together. A certificate alone signs the app and the image with the
Developer ID and notarizes nothing. That is neither mode, and what macOS shows for it has not been observed.
Notarization credentials without a certificate fail the job, because Apple accepts only a Developer ID signature and
`tools/release/notarize_macos.sh` stops when it finds none. With credentials, a failing step fails the job, so a file
that Apple refused is never published as notarized. Without credentials the job succeeds and the files are not
notarized.

### What the owner adds

The owner adds these to the environment `macos-signing` in the repository settings and limits the environment to the
`production` branch and `v*` tags, the runs that use it. A secret is stored as a secret of the environment and a
variable as a variable of it. None of them is committed, and the scripts print none of the values. The job imports the
certificate into a keychain of its own and deletes that keychain in its last step.

| Name | Kind | What it holds |
| --- | --- | --- |
| `MACOS_CERTIFICATE` | Secret | The Developer ID Application certificate with its private key, as a `.p12` file, base64 encoded |
| `MACOS_CERTIFICATE_PASSWORD` | Secret | The password of the `.p12` file |
| `MACOS_SIGNING_IDENTITY` | Variable | The name of the identity, as `security find-identity` prints it |
| `MACOS_NOTARY_KEY` | Secret | For an App Store Connect API key: the text of the `.p8` file |
| `MACOS_NOTARY_KEY_ID` | Variable | For an API key: the key id |
| `MACOS_NOTARY_ISSUER_ID` | Variable | For an API key: the issuer id |
| `MACOS_NOTARY_APPLE_ID` | Variable | For an Apple ID: the Apple ID |
| `MACOS_NOTARY_TEAM_ID` | Variable | For an Apple ID: the team id |
| `MACOS_NOTARY_PASSWORD` | Secret | For an Apple ID: an app-specific password |

Notarization needs all three entries of one group, the API key or the Apple ID. The API key is used when both groups are
complete. A group that is only partly set, with the other group not complete, fails the job and names what is missing.
A certificate without its password or without the identity name fails it too. The scripts are described in
[macOS signing script](build-and-test.md#macos-signing-script) and
[macOS notarization script](build-and-test.md#macos-notarization-script). The roles in [Ownership](#ownership) and the
rules in [Repository authorization](#repository-authorization) apply to these values.

### Apple Developer identity record

Before anyone buys a personal Apple Developer Program membership, someone asks whether an institutional or distributor
team can sign for EGTRAIN and whether a fee waiver of the programme applies. These are messages to people, for example
at the university, and the table records the answers. Not recorded means that nothing has been recorded. Without the
settings above the release is built in the ad hoc mode.

| Item | Record |
| --- | --- |
| Who was asked about an institutional or distributor Apple Developer team, the date, and the link to the answer | Not recorded |
| Result: a team that can sign for EGTRAIN, or none | Not recorded |
| Who was asked about a fee waiver of the Apple Developer Program, the date, and the link to the answer | Not recorded |
| Result: a waiver, or none | Not recorded |
| Decision taken, and by whom (a team, a waiver, a personal membership, or staying ad hoc) | Not recorded |
| Developer ID Application identity as issued, and the team id | Not recorded |
| Notarization credentials used (API key or Apple ID) | Not recorded |
| Expiry date of the certificate, and who watches it | Not recorded |
| Environment `macos-signing` created and limited to `production` and `v*` | Not recorded |

### First start of an ad hoc build

This was observed on 2026-10-16 on a clean macOS 26.6.2 with the setting **Allow applications from** at **App Store &
Known Developers**, for a disk image that carried the download mark that a browser sets. Opening the image shows a
Finder window named `EGTRAIN` with `QEGTRAIN` and `Applications`, and no warning. After the app is copied to the
Applications folder, opening it from there shows:

1. A dialog titled `"QEGTRAIN" Not Opened`. Its text is
   `Apple could not verify "QEGTRAIN" is free of malware that may harm your Mac or compromise your privacy.`
   Its buttons are **Move to Trash** and **Done**. It has no **Open** button.
2. **System Settings > Privacy & Security**, in the section **Security**, shows
   `"QEGTRAIN" was blocked to protect your Mac.` with the button **Open Anyway**.
3. **Open Anyway** opens a dialog titled `Open "QEGTRAIN"?` with the buttons **Move to Trash**, **Open Anyway** and
   **Done**.
4. **Open Anyway** in that dialog asks for an administrator's name and password:
   `Enter an administrator's username and password to allow this.` The exception needs an administrator. What follows
   the password was not observed.

This is the normal macOS override for an app from an unidentified developer, and the only way to open an ad hoc build
that this project documents. No workflow and no release script removes the download mark (the quarantine attribute) or
turns Gatekeeper off, and no document tells a user to, because that lowers the protection of the whole Mac.
`tools/e2e/test_ci_workflow.py` checks the workflows and the release scripts.

### First start of a notarized build

The app and the image carry a Developer ID signature and a ticket from Apple that is stapled to each. A user who opens
the image, drags the app onto `Applications` and opens it is expected to see no dialog beyond the confirmation that
macOS asks for an app that was downloaded, and to need no visit to System Settings and no administrator password. No
release of EGTRAIN has been built in this mode, so this has not been observed. Row M5 of the
[release testing checklist](release-testing-checklist.md#macos-app) records it for the first release that has one.

### Release text

The release text that the Prepare release step writes says nothing about signing. After a release is published, the owner
adds one of these paragraphs to its description on the releases page. The first is for a release whose app is signed ad
hoc: the log of the step **Verify the signature** of the package job shows `Signature=adhoc`. The second is only for one
whose app and image are notarized: the logs of the steps **Notarize the app** and **Notarize the disk image** each end
with a line `OK: ... is notarized, stapled and accepted by Gatekeeper`. A Developer ID signature with the line
`Notarization skipped` is neither, and needs a sentence of its own.

```text
macOS: the app is signed ad hoc and is not notarized by Apple, so macOS refuses the first start. To allow it, choose Open Anyway for QEGTRAIN in System Settings > Privacy & Security. macOS then asks for an administrator's name and password. See "macOS disk image" in the README: https://github.com/Ancientkingg/EGTRAIN#macos-disk-image
```

```text
macOS: the app and the disk image are signed with a Developer ID and notarized by Apple. macOS is expected to ask only for the usual confirmation for an app that was downloaded.
```

### Checking a macOS download

Run these on the copy of the app in the Applications folder. They do not start the app.

```bash
codesign -dvv /Applications/QEGTRAIN.app
xcrun stapler validate /Applications/QEGTRAIN.app
```

`codesign` shows `Signature=adhoc` for an ad hoc build. For a Developer ID build it shows a line
`Authority=Developer ID Application:` with the name of the signer, and a `TeamIdentifier`. `stapler validate` succeeds
only when the app carries a ticket and fails for an ad hoc build. Before it uploads the image, the package job checks the
signature in the step **Verify the signature** and, for a notarized build, the ticket in the step **Notarize the app**.
