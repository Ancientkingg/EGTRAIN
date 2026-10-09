# Code signing

This document covers Authenticode signing of the Windows program files and the Windows installer. It records state,
ownership and checks, and it signs nothing. It states no decision of any signing service: every fact that only the owner
can give is a field that reads Not recorded.

## Current state

- No Windows file is signed. No workflow step signs a Windows file or checks a signature, and the workflows use no
  credential other than the token that GitHub gives to each run.
- The Windows download is the portable ZIP `QEGTRAIN-windows-x64.zip`. There is no installer (#401).
- The macOS app is ad-hoc signed in the release workflow. An ad-hoc signature carries no publisher identity. Developer
  ID signing for macOS is #404 and is not covered here. The Linux AppImage is not signed.
- The in-app update checks the SHA-256 and the size of the downloaded package against `update-manifest.json`, an asset
  of the same release. On Windows it checks no signature, so these checks detect a damaged download and do not show who
  published the package. On macOS the staged app is also checked with `codesign --verify`, which an ad-hoc signature
  passes.
- A release page does not say whether the Windows files are signed. The release text, built by the Prepare release step
  of the release job in `.github/workflows/release.yml`, says nothing about signing.

## What would be signed

The files are `QEGTRAIN.exe`, `egtrain_update_helper.exe`, `scene_tool.exe` and the Windows installer when it exists.
The ZIP and the installer must contain the same signed program files, so signing happens before the ZIP is built and
before the installer is compiled. The Generate update manifest step of the release job takes the SHA-256 and the size of
the ZIP that the Windows package job uploads, so that ZIP must already hold the signed files. A ZIP file carries no
Authenticode signature of its own. Signatures use SHA-256 and a trusted timestamp where the signing service supports
them.

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

- Credentials exist only in a GitHub environment that only the `production` branch and `v*` tags may use. They are never
  in the repository, in workflow logs, in workflow artifacts or in release assets. No certificate, private key, password
  or token is committed.
- Signing runs only for pushes to `production` and for `v*` tags. Pull requests, including pull requests from forks and
  the pull request from `main` to `production`, and manual runs never sign and never need credentials. Ordinary
  development builds and the checks of pull requests stay unsigned and fully testable.
- A signing step is skipped, not failed, when nothing is configured, so the portable ZIP and the installer stay usable
  unsigned.
- When signing is configured, the release fails if a required file is unsigned, invalid or signed by an unexpected
  signer, and the ZIP and the installer are built from the files that were checked.

## Renewal and maintenance

| Item | Record |
| --- | --- |
| Validity period of the certificate or of the service agreement | Not recorded |
| Who watches the expiry, and how | Not recorded |
| Rotation of the access token of the signing service (interval, last done, next due) | Not recorded |
| What to update when the signer name or the service changes (the expected signer name used by the check) | Not recorded |

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
signed. Because no workflow signs a Windows file, the current Windows downloads show `NotSigned`.

## Unsigned downloads

Until a release is signed, Windows may warn before it starts a program that was downloaded from the internet and has no
publisher signature (the SmartScreen screen "Windows protected your PC"). The wording and the options depend on the
Windows version. The project has not recorded how the screen looks on a clean machine: the release rehearsal records no
Windows desktop check.

A valid signature shows who signed a file. Whether Windows still warns for a signed program depends on the Windows
version and on checks of its own that the project has not observed. This document makes no promise either way. When a
release is signed, its release text must say which Windows files are signed.
