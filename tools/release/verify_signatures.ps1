<#
.SYNOPSIS
Checks the Authenticode signatures of the Windows program files of a package or of an installer.

.DESCRIPTION
  verify_signatures.ps1 -Directory DIR (-Require | -Report) [-ExpectedSubject SUBJECT] [-Name FILE,...] [-AllowUntrustedRoot] [-NoTimestampCheck]

Each file in -Name must be in -Directory. -Name is QEGTRAIN.exe, egtrain_update_helper.exe and scene_tool.exe unless it is given. A
file passes when
1. its signature status is Valid,
2. the digest algorithm of its signature is SHA-256,
3. its signature has a timestamp, and
4. the subject of the signing certificate is -ExpectedSubject.

-Require exits with 1 when a file does not pass, and it needs -ExpectedSubject. -Report prints the same findings and always exits with
0; it checks the subject only when -ExpectedSubject is given. Give exactly one of them.

-AllowUntrustedRoot and -NoTimestampCheck are for tests that sign with a throwaway certificate. The first also accepts a signature whose
certificate chain ends in a root that this machine does not trust, and the second skips the timestamp check. Do not use them on a release.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [string[]]$Name = @('QEGTRAIN.exe', 'egtrain_update_helper.exe', 'scene_tool.exe'),
    [string]$ExpectedSubject = '',
    [switch]$Require,
    [switch]$Report,
    [switch]$AllowUntrustedRoot,
    [switch]$NoTimestampCheck
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not ($Require.IsPresent -xor $Report.IsPresent)) { throw 'Give exactly one of -Require and -Report.' }
if ($Require -and -not $ExpectedSubject) { throw '-Require needs -ExpectedSubject.' }
if (-not (Test-Path -LiteralPath $Directory -PathType Container)) { throw "No directory $Directory." }

# SignedCms is in System.Security.Cryptography.Pkcs in PowerShell 7 and in System.Security in Windows PowerShell.
foreach ($assembly in 'System.Security.Cryptography.Pkcs', 'System.Security') {
    try { Add-Type -AssemblyName $assembly -ErrorAction Stop; break } catch { }
}

$sha256 = '2.16.840.1.101.3.4.2.1'

# Get-AuthenticodeSignature does not report the digest algorithm of a signature, so this reads it from the file. The signature is a
# PKCS #7 block in the certificate table of the PE file. Returns the object identifier and the name of the algorithm.
function Get-SignatureDigest([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $optional = [BitConverter]::ToInt32($bytes, 0x3C) + 24
    $directories = $optional + 96
    if ([BitConverter]::ToUInt16($bytes, $optional) -eq 0x20B) { $directories = $optional + 112 }
    # The fifth data directory is the certificate table. Its address is a file offset. Each entry starts with an 8 byte header.
    $offset = [int][BitConverter]::ToUInt32($bytes, $directories + 32)
    if ($offset -eq 0) { throw 'the file has no certificate table' }
    $start = $offset + 8
    # The block is a DER sequence, and the table pads it, which the decoder may not accept.
    $lengthSize = 0
    $length = [int]$bytes[$start + 1]
    if ($length -ge 0x80) {
        $lengthSize = $length -band 0x7F
        $length = 0
        for ($i = 0; $i -lt $lengthSize; $i++) { $length = $length * 256 + $bytes[$start + 2 + $i] }
    }
    $block = New-Object byte[] (2 + $lengthSize + $length)
    [Array]::Copy($bytes, $start, $block, 0, $block.Length)
    $cms = New-Object System.Security.Cryptography.Pkcs.SignedCms
    $cms.Decode($block)
    $algorithm = $cms.SignerInfos[0].DigestAlgorithm
    return @($algorithm.Value, $algorithm.FriendlyName)
}

# Returns the problems of one file, and the subject of its signing certificate when it has one.
function Test-File([string]$Path) {
    $problems = @()
    $subject = ''
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        $problems += 'the file is missing'
    } else {
        $signature = Get-AuthenticodeSignature -LiteralPath $Path
        $status = [string]$signature.Status
        if ($status -eq 'NotSigned') {
            $problems += 'not signed'
        } else {
            $accepted = @('Valid')
            if ($AllowUntrustedRoot) { $accepted += @('UnknownError', 'NotTrusted') }
            if ($accepted -notcontains $status) { $problems += "status ${status}: $($signature.StatusMessage)" }
            if ($signature.SignerCertificate) { $subject = $signature.SignerCertificate.Subject }
            if ($ExpectedSubject -and $subject -ne $ExpectedSubject) { $problems += "signed by '$subject', not by '$ExpectedSubject'" }
            if (-not $NoTimestampCheck -and -not $signature.TimeStamperCertificate) { $problems += 'no timestamp' }
            $digest = Get-SignatureDigest $Path
            if ($digest[0] -ne $sha256) { $problems += "digest algorithm $($digest[1]) ($($digest[0])), not SHA-256" }
        }
    }
    return [pscustomobject]@{ Problems = $problems; Subject = $subject }
}

$failed = 0
foreach ($file in $Name) {
    try {
        $result = Test-File (Join-Path $Directory $file)
    } catch {
        $result = [pscustomobject]@{ Problems = @("could not be checked: $($_.Exception.Message)"); Subject = '' }
    }
    if ($result.Problems.Count -eq 0) {
        Write-Host "${file}: meets the checks, signed by $($result.Subject)"
    } else {
        $failed++
        Write-Host "${file}: $($result.Problems -join '; ')"
    }
}

Write-Host "$($Name.Count - $failed) of $($Name.Count) files meet the checks."
if ($Require -and $failed -gt 0) { exit 1 }
exit 0
