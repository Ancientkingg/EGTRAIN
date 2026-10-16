<#
.SYNOPSIS
Tests verify_signatures.ps1 with a throwaway code-signing certificate.

.DESCRIPTION
  test_verify_signatures.ps1 -SceneTool EXE

Run it on Windows. It creates a self-signed code-signing certificate in the certificate store of the current user, signs copies of
-SceneTool in a temporary directory and runs verify_signatures.ps1 on them, in a new PowerShell for each run. The certificate and its
key exist for the run only and are removed at the end. The certificate is not exported, nothing is written below the repository and
nothing is added to the trusted roots, so the verifier needs -AllowUntrustedRoot to accept the signed copies. The signature carries
no timestamp, so most runs also need -NoTimestampCheck.

It expects:
1. a copy that is signed with SHA-256 by the certificate passes;
2. an unsigned copy, a copy signed with SHA-1, a copy checked against another subject, a missing file, a copy whose certificate
   chain is not trusted without -AllowUntrustedRoot, and a copy without a timestamp without -NoTimestampCheck each fail, and the
   output names that reason;
3. -Report prints the findings for an unsigned copy and exits with 0.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$SceneTool
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$SceneTool = (Resolve-Path -LiteralPath $SceneTool).Path
$verifier = Join-Path $PSScriptRoot 'verify_signatures.ps1'
$shell = (Get-Process -Id $PID).Path
$work = Join-Path ([System.IO.Path]::GetTempPath()) ('egtrain-verify-signatures-' + [guid]::NewGuid().ToString('N'))
$subject = 'CN=EGTRAIN-test-' + [guid]::NewGuid().ToString('N')
$certificate = $null

# Copies scene_tool.exe into a new directory of the work directory, signs the copy when a digest algorithm is given, and returns the
# directory.
function New-Copy([string]$Directory, [string]$HashAlgorithm) {
    $path = Join-Path $work $Directory
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    $file = Join-Path $path 'scene_tool.exe'
    Copy-Item -LiteralPath $SceneTool -Destination $file
    if ($HashAlgorithm) {
        $signature = Set-AuthenticodeSignature -FilePath $file -Certificate $certificate -HashAlgorithm $HashAlgorithm
        if ([string]$signature.Status -eq 'NotSigned') { throw "The copy $Directory was not signed." }
    }
    return $path
}

# Runs the verifier on a directory in a new PowerShell and checks its exit code and that its output matches the pattern.
function Test-Verifier([string]$What, [string]$Directory, [string[]]$Arguments, [int]$ExitCode, [string]$Pattern) {
    $saved = $ErrorActionPreference
    # Windows PowerShell turns a line on the error stream of a program into an error when the preference is Stop.
    $ErrorActionPreference = 'Continue'
    try {
        $output = & $shell -NoProfile -ExecutionPolicy Bypass -File $verifier -Directory $Directory @Arguments 2>&1 | Out-String
    } finally {
        $ErrorActionPreference = $saved
    }
    $code = $LASTEXITCODE
    if ($code -ne $ExitCode -or $output -notmatch $Pattern) {
        throw "${What}: exit code $code, expected $ExitCode, and the output must match '$Pattern':`n$output"
    }
    Write-Host "ok: $What"
}

try {
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    New-SelfSignedCertificate -Type CodeSigningCert -Subject $subject -CertStoreLocation Cert:\CurrentUser\My `
        -HashAlgorithm SHA256 -NotAfter (Get-Date).AddDays(1) | Out-Null
    # The certificate is read from the store, because the object that the command returns may lack the private key.
    $certificate = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject } | Select-Object -First 1
    if (-not $certificate -or -not $certificate.HasPrivateKey) { throw 'The throwaway certificate has no private key.' }

    $signed = New-Copy 'signed' 'SHA256'
    $sha1 = New-Copy 'sha1' 'SHA1'
    $unsigned = New-Copy 'unsigned' ''
    $name = @('-Name', 'scene_tool.exe')
    $require = @('-Require', '-ExpectedSubject', $subject)
    $untrusted = @('-AllowUntrustedRoot', '-NoTimestampCheck')

    Test-Verifier 'a copy signed with SHA-256 passes' $signed ($name + $require + $untrusted) 0 'meets the checks, signed by CN=EGTRAIN-test'
    Test-Verifier 'an unsigned copy fails' $unsigned ($name + $require + $untrusted) 1 'not signed'
    Test-Verifier 'a copy signed with SHA-1 fails' $sha1 ($name + $require + $untrusted) 1 'not SHA-256'
    Test-Verifier 'another subject fails' $signed ($name + '-Require' + '-ExpectedSubject' + 'CN=Someone-else' + $untrusted) 1 'not by ''CN=Someone-else'''
    Test-Verifier 'a missing file fails' $signed (@('-Name', 'absent.exe') + $require + $untrusted) 1 'the file is missing'
    Test-Verifier 'an untrusted chain fails without -AllowUntrustedRoot' $signed ($name + $require + '-NoTimestampCheck') 1 'status \w+'
    Test-Verifier 'a missing timestamp fails without -NoTimestampCheck' $signed ($name + $require + '-AllowUntrustedRoot') 1 'no timestamp'
    Test-Verifier '-Report prints the findings and passes' $unsigned ($name + '-Report') 0 'scene_tool.exe: not signed'
    Test-Verifier '-Require without a subject is refused' $signed ($name + '-Require') 1 'needs -ExpectedSubject'
    Write-Host 'All signature checks passed.'
} finally {
    try {
        foreach ($item in @(Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq $subject })) {
            Remove-Item -LiteralPath $item.PSPath -DeleteKey
        }
    } catch {
        Write-Warning "The throwaway certificate stays in the store of the current user: $($_.Exception.Message)"
    }
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}
