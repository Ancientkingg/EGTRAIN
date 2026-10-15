<#
.SYNOPSIS
Installs, upgrades and uninstalls the EGTRAIN installer on this machine and checks what each step leaves behind.

.DESCRIPTION
Run it on a Windows machine that has no EGTRAIN installation. It changes the registry, the Start Menu and the desktop of the
current user and %LOCALAPPDATA%\Programs\EGTRAIN, and it removes what it installs.

  installer_smoke.ps1 -Installer EXE -UpgradeInstaller EXE -Payload DIR -Version X.Y.Z -UpgradeVersion X.Y.Z [-Work DIR] [-Python NAME]

-Installer and -UpgradeInstaller are two installers of the same payload, built with /DVersion=-Version and with a higher
/DVersion=-UpgradeVersion. -Payload is the package directory they were built from. -Work is a directory that does not exist or
is empty. The script puts its installations, the files outside them and the logs of Setup and of the uninstaller below it.
-Python names the interpreter that runs package_start_smoke.py.

1. Both installers ask for the invoking user's rights (asInvoker) in their manifest.
2. Silent install into a directory of -Work, without the desktop task. The only uninstall entry is under HKEY_CURRENT_USER and
   names the directory and the version. None is under HKEY_LOCAL_MACHINE. The Start Menu shortcut starts QEGTRAIN.exe in the
   directory, and no desktop shortcut exists. The directory holds the files of the payload with the same bytes, the marker
   file of the installer and one uninstaller, and nothing else. package_start_smoke.py starts the installed copy.
   The silent uninstall then removes the files, the shortcut and the entry, and a file beside the directory survives. A file
   that the program wrote into the directory during the start check may stay with the directory.
3. The same with /TASKS=desktopicon: the desktop shortcut exists after the install and is gone after the uninstall.
4. Install into the default directory, which is %LOCALAPPDATA%\Programs\EGTRAIN. Plant a scene folder that the payload does not
   have. Install -UpgradeInstaller over it without a directory: the entry and the directory stay one, the entry names the
   higher version, the planted folder is gone and the files are those of the payload. The uninstall leaves a file outside the
   directory.

The script does not show that a standard user can install without an elevation request: the account of a CI runner is an
administrator.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$UpgradeInstaller,
    [Parameter(Mandatory = $true)][string]$Payload,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$UpgradeVersion,
    [string]$Work = (Join-Path $env:TEMP 'egtrain-installer-smoke'),
    [string]$Python = 'python'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Work = [System.IO.Path]::GetFullPath($Work)
$Installer = (Resolve-Path -LiteralPath $Installer).Path
$UpgradeInstaller = (Resolve-Path -LiteralPath $UpgradeInstaller).Path
$Payload = (Resolve-Path -LiteralPath $Payload).Path.TrimEnd('\')
$repository = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$startSmoke = Join-Path $PSScriptRoot 'package_start_smoke.py'

# The AppId line of the script is "AppId={{GUID}": Inno Setup reads the doubled brace as one literal brace.
$idLine = Select-String -LiteralPath (Join-Path $repository 'installer\egtrain.iss') -Pattern '^AppId=\{(\{[0-9A-Fa-f-]+\})\s*$' |
    Select-Object -First 1
if (-not $idLine) { throw 'installer\egtrain.iss has no AppId line' }
$appId = $idLine.Matches[0].Groups[1].Value
$entryName = "${appId}_is1"
$userRoot = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall'
$registryRoots = @(
    $userRoot,
    'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall',
    'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'
)
$startMenuLink = Join-Path ([Environment]::GetFolderPath('Programs')) 'EGTRAIN.lnk'
$desktopLink = Join-Path ([Environment]::GetFolderPath('Desktop')) 'EGTRAIN.lnk'
$defaultDirectory = Join-Path $env:LOCALAPPDATA 'Programs\EGTRAIN'
$logs = Join-Path $Work 'logs'

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Wait-Until([scriptblock]$Condition, [string]$What, [int]$Seconds = 120) {
    $deadline = (Get-Date).AddSeconds($Seconds)
    while (-not (& $Condition)) {
        if ((Get-Date) -gt $deadline) { throw "Gave up waiting until $What." }
        Start-Sleep -Milliseconds 500
    }
}

function Get-Property($Properties, [string]$Name) {
    $property = $Properties.PSObject.Properties[$Name]
    if ($property) { return $property.Value }
    return $null
}

function Test-SamePath([string]$First, [string]$Second) {
    if ([string]::IsNullOrEmpty($First) -or [string]::IsNullOrEmpty($Second)) { return $false }
    $firstFull = [System.IO.Path]::GetFullPath($First).TrimEnd('\')
    $secondFull = [System.IO.Path]::GetFullPath($Second).TrimEnd('\')
    return [string]::Equals($firstFull, $secondFull, [System.StringComparison]::OrdinalIgnoreCase)
}

# Compares the bytes with fc.exe, which Windows has. fc without the extension is an alias of Format-Custom.
function Test-SameContent([string]$Left, [string]$Right) {
    if ((Get-Item -LiteralPath $Left).Length -ne (Get-Item -LiteralPath $Right).Length) { return $false }
    & (Join-Path $env:SystemRoot 'System32\fc.exe') /b $Left $Right | Out-Null
    return ($LASTEXITCODE -eq 0)
}

function Get-RelativeFiles([string]$Directory) {
    $prefix = (Get-Item -LiteralPath $Directory).FullName.TrimEnd('\') + '\'
    Get-ChildItem -LiteralPath $Directory -Recurse -File -Force | ForEach-Object { $_.FullName.Substring($prefix.Length) }
}

# The full names of the uninstall entries of EGTRAIN in the registry of the user and of the machine, in both views.
function Get-UninstallEntries {
    foreach ($root in $registryRoots) {
        if (Test-Path -LiteralPath $root) {
            Get-ChildItem -LiteralPath $root |
                Where-Object { $_.PSChildName.StartsWith($appId, [System.StringComparison]::OrdinalIgnoreCase) } |
                ForEach-Object { $_.Name }
        }
    }
}

function Show-Log([string]$Log) {
    if (Test-Path -LiteralPath $Log) {
        Write-Host "--- last lines of $Log"
        Get-Content -LiteralPath $Log -Tail 40 | ForEach-Object { Write-Host $_ }
    }
}

function Invoke-Setup([string]$Program, [string]$Name, [string[]]$Arguments) {
    $log = Join-Path $logs "$Name.log"
    $all = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', "/LOG=`"$log`"") + $Arguments
    Write-Host "Running $Program $($all -join ' ')"
    $process = Start-Process -FilePath $Program -ArgumentList $all -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        Show-Log $log
        throw "$Name exited with $($process.ExitCode)"
    }
}

function Install-Egtrain([string]$Program, [string]$Name, [string]$Directory, [string[]]$Arguments = @()) {
    $all = $Arguments
    if ($Directory) { $all = @("/DIR=`"$Directory`"") + $Arguments }
    Invoke-Setup $Program $Name $all
}

function Uninstall-Egtrain([string]$Directory, [string]$Name) {
    $uninstallers = @(Get-ChildItem -LiteralPath $Directory -Filter 'unins*.exe')
    Assert-True ($uninstallers.Count -eq 1) "expected one uninstaller in $Directory, found $($uninstallers.Count)"
    Invoke-Setup $uninstallers[0].FullName $Name @()
}

function Assert-AsInvoker([string]$Program) {
    # The manifest is a resource of the program and is stored as text, in UTF-8 as a rule and else in UTF-16.
    $bytes = [System.IO.File]::ReadAllBytes($Program)
    $pattern = 'requestedExecutionLevel\s+level\s*=\s*["'']([A-Za-z]+)["'']'
    $match = [regex]::Match([System.Text.Encoding]::GetEncoding(28591).GetString($bytes), $pattern)
    if (-not $match.Success) { $match = [regex]::Match([System.Text.Encoding]::Unicode.GetString($bytes), $pattern) }
    Assert-True $match.Success "$Program has no requestedExecutionLevel in its manifest"
    Assert-True ($match.Groups[1].Value -eq 'asInvoker') "$Program requests $($match.Groups[1].Value), not asInvoker"
    Write-Host "PASS $(Split-Path -Leaf $Program) requests asInvoker"
}

function Assert-Registered([string]$Directory, [string]$ExpectedVersion) {
    $entries = @(Get-UninstallEntries)
    $machine = @($entries | Where-Object { $_ -like 'HKEY_LOCAL_MACHINE\*' })
    Assert-True ($machine.Count -eq 0) "found an uninstall entry under HKEY_LOCAL_MACHINE: $($machine -join ', ')"
    $user = @($entries | Where-Object { $_ -like 'HKEY_CURRENT_USER\*' })
    Assert-True ($user.Count -eq 1) "expected one uninstall entry under HKEY_CURRENT_USER, found $($user.Count)"
    Assert-True (Test-Path -LiteralPath "$userRoot\$entryName") "the uninstall entry is not named $entryName"
    $properties = Get-ItemProperty -LiteralPath "$userRoot\$entryName"
    $location = Get-Property $properties 'Inno Setup: App Path'
    if (-not $location) { $location = Get-Property $properties 'InstallLocation' }
    Assert-True (Test-SamePath $location $Directory) "the uninstall entry names '$location', not $Directory"
    $registered = Get-Property $properties 'DisplayVersion'
    Assert-True ($registered -eq $ExpectedVersion) "the uninstall entry shows version '$registered', not $ExpectedVersion"
    Write-Host "PASS one uninstall entry under HKEY_CURRENT_USER for $Directory, version $ExpectedVersion, none under HKEY_LOCAL_MACHINE"
}

function Assert-Shortcut([string]$Link, [string]$Directory) {
    Assert-True (Test-Path -LiteralPath $Link -PathType Leaf) "missing shortcut $Link"
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($Link)
    Assert-True (Test-SamePath $shortcut.TargetPath (Join-Path $Directory 'QEGTRAIN.exe')) "$Link starts '$($shortcut.TargetPath)'"
    Assert-True (Test-SamePath $shortcut.WorkingDirectory $Directory) "$Link starts in '$($shortcut.WorkingDirectory)'"
    Write-Host "PASS $Link starts QEGTRAIN.exe of $Directory"
}

function Assert-InstalledFiles([string]$Directory) {
    $expected = @(Get-RelativeFiles $Payload)
    foreach ($relative in $expected) {
        $installed = Join-Path $Directory $relative
        Assert-True (Test-Path -LiteralPath $installed -PathType Leaf) "the installed copy lacks $relative"
        Assert-True (Test-SameContent (Join-Path $Payload $relative) $installed) "$relative differs from the package"
    }
    $marker = Join-Path $Directory 'installed-by-setup.ini'
    Assert-True (Test-Path -LiteralPath $marker -PathType Leaf) 'the installed copy lacks installed-by-setup.ini'
    Assert-True (Test-SameContent (Join-Path $repository 'installer\installed-by-setup.ini') $marker) 'installed-by-setup.ini differs from the repository file'
    $other = @(Get-RelativeFiles $Directory | Where-Object {
        $expected -notcontains $_ -and $_ -ne 'installed-by-setup.ini' -and $_ -notmatch '^unins\d+\.(exe|dat|msg)$'
    })
    Assert-True ($other.Count -eq 0) "the installed copy holds files that are not in the package: $($other -join ', ')"
    $uninstallers = @(Get-ChildItem -LiteralPath $Directory -Filter 'unins*.exe')
    Assert-True ($uninstallers.Count -eq 1) "expected one uninstaller in $Directory, found $($uninstallers.Count)"
    Write-Host "PASS $Directory holds the $($expected.Count) files of the package, the marker file and one uninstaller"
}

function Invoke-StartSmoke([string]$Directory) {
    $env:PYTHONUTF8 = '1'
    & $Python $startSmoke $Directory
    if ($LASTEXITCODE -ne 0) { throw "package_start_smoke.py failed for $Directory with exit code $LASTEXITCODE" }
}

# $Kept lists the files that may stay in the directory, because the program wrote them.
function Assert-Uninstalled([string]$Directory, [string[]]$Kept) {
    Wait-Until { @(Get-UninstallEntries).Count -eq 0 } 'the uninstall entry is removed'
    Wait-Until { -not (Test-Path -LiteralPath $startMenuLink) -and -not (Test-Path -LiteralPath $desktopLink) } 'the shortcuts are removed'
    if ($Kept.Count -eq 0) {
        Wait-Until { -not (Test-Path -LiteralPath $Directory) } "$Directory is removed"
    } else {
        Wait-Until {
            -not (Test-Path -LiteralPath $Directory) -or @(Get-RelativeFiles $Directory | Where-Object { $Kept -notcontains $_ }).Count -eq 0
        } "only the files of the program are left in $Directory"
    }
    Write-Host "PASS the uninstall removed the entry, the shortcuts and the files of $Directory"
}

function Assert-Survives([string]$Path) {
    Assert-True (Test-Path -LiteralPath $Path -PathType Leaf) "$Path did not survive"
    Assert-True ((Get-Content -LiteralPath $Path -Raw).Trim() -eq 'user file') "$Path was changed"
    Write-Host "PASS $Path survived"
}

function New-UserFile([string]$Path) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    Set-Content -LiteralPath $Path -Value 'user file'
}

# Best effort after a failure, so that the machine is left as it was found.
function Remove-Leftover {
    try {
        if (-not (Test-Path -LiteralPath "$userRoot\$entryName")) { return }
        $properties = Get-ItemProperty -LiteralPath "$userRoot\$entryName"
        $location = Get-Property $properties 'Inno Setup: App Path'
        if (-not $location) { $location = Get-Property $properties 'InstallLocation' }
        if ($location -and (Test-Path -LiteralPath $location)) {
            foreach ($uninstaller in @(Get-ChildItem -LiteralPath $location -Filter 'unins*.exe')) {
                Start-Process -FilePath $uninstaller.FullName -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait
            }
        }
    } catch {
        Write-Host "Could not remove the installation that is left: $_"
    }
}

if ((Test-Path -LiteralPath $Work) -and @(Get-ChildItem -LiteralPath $Work -Force).Count -gt 0) {
    throw "$Work exists and is not empty"
}
New-Item -ItemType Directory -Force -Path $logs | Out-Null
Assert-True (@(Get-UninstallEntries).Count -eq 0) 'EGTRAIN is already installed here; run the smoke test on a machine without it'
foreach ($path in @($startMenuLink, $desktopLink, $defaultDirectory)) {
    Assert-True (-not (Test-Path -LiteralPath $path)) "$path exists already; run the smoke test on a machine without EGTRAIN"
}

try {
    Assert-AsInvoker $Installer
    Assert-AsInvoker $UpgradeInstaller

    $directory = Join-Path $Work 'plain\EGTRAIN'
    $outside = Join-Path $Work 'plain\notes.txt'
    New-UserFile $outside
    Install-Egtrain $Installer 'plain-install' $directory
    Assert-Registered $directory $Version
    Assert-Shortcut $startMenuLink $directory
    Assert-True (-not (Test-Path -LiteralPath $desktopLink)) 'a silent install without the task created a desktop shortcut'
    Assert-InstalledFiles $directory
    $before = @(Get-RelativeFiles $directory)
    Invoke-StartSmoke $directory
    $written = @(Get-RelativeFiles $directory | Where-Object { $before -notcontains $_ })
    Write-Host "The start check wrote $($written.Count) files into the installation directory."
    Uninstall-Egtrain $directory 'plain-uninstall'
    Assert-Uninstalled $directory $written
    Assert-Survives $outside

    $directory = Join-Path $Work 'icon\EGTRAIN'
    Install-Egtrain $Installer 'icon-install' $directory @('/TASKS=desktopicon')
    Assert-Registered $directory $Version
    Assert-Shortcut $startMenuLink $directory
    Assert-Shortcut $desktopLink $directory
    Uninstall-Egtrain $directory 'icon-uninstall'
    Assert-Uninstalled $directory @()

    $outside = Join-Path $Work 'upgrade\notes.txt'
    New-UserFile $outside
    Install-Egtrain $Installer 'upgrade-install'
    Assert-Registered $defaultDirectory $Version
    Assert-InstalledFiles $defaultDirectory
    $stale = Join-Path $defaultDirectory 'Scenes\RemovedInNewerVersion'
    New-UserFile (Join-Path $stale 'scene.json')
    Install-Egtrain $UpgradeInstaller 'upgrade-over'
    Assert-Registered $defaultDirectory $UpgradeVersion
    Assert-True (-not (Test-Path -LiteralPath $stale)) 'the Scenes folder of the older version was not replaced'
    Assert-InstalledFiles $defaultDirectory
    $directories = @(Get-ChildItem -LiteralPath (Split-Path -Parent $defaultDirectory) -Directory -Filter 'EGTRAIN*')
    Assert-True ($directories.Count -eq 1) "expected one installation directory, found $($directories.Count)"
    Assert-Shortcut $startMenuLink $defaultDirectory
    Uninstall-Egtrain $defaultDirectory 'upgrade-uninstall'
    Assert-Uninstalled $defaultDirectory @()
    Assert-Survives $outside
}
finally {
    Remove-Leftover
}

Write-Host 'OK: the installer installs, upgrades and uninstalls for the current user as checked'
