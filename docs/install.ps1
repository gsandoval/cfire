# cfire installer for Windows (PowerShell 5.1+ / 7+)
# Usage:
#   irm https://cfire.memo.engineer/install.ps1 | iex
# Optional environment variables:
#   $env:CFIRE_VERSION          - Specific tag (e.g. "v0.11.0")
#   $env:CFIRE_INSTALL_DIR      - Custom install directory (default: $env:LOCALAPPDATA\Programs\cfire)
#   $env:CFIRE_RELEASE_BASE_URL - Custom mirror or local file:// release base URL
#   $env:CFIRE_SETUP            - Set to "1" to run 'cfire setup --yes' after installation
#   $env:CFIRE_NO_MODIFY_PATH   - Set to "1" to skip modifying User PATH
param(
    [switch]$Setup
)

$ErrorActionPreference = 'Stop'

Write-Host "Installing cfire (pronounced sapphire) for Windows..." -ForegroundColor White

$ArchRaw = ""
try {
    $ArchRaw = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
} catch {
    $ArchRaw = $env:PROCESSOR_ARCHITECTURE
}

switch -Regex ($ArchRaw.ToUpperInvariant()) {
    '^(X64|AMD64)$' { $TargetArch = "windows-x86_64" }
    '^(ARM64)$'     { $TargetArch = "windows-arm64" }
    default         { throw "Unsupported Windows architecture: $ArchRaw" }
}

function Download-CfireFile {
    param(
        [string]$Uri,
        [string]$OutFile
    )
    $Parsed = $null
    if ([System.Uri]::TryCreate($Uri, [System.UriKind]::Absolute, [ref]$Parsed) -and $Parsed.IsFile) {
        Copy-Item -Path $Parsed.LocalPath -Destination $OutFile -Force
        return
    }
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    } catch {}
    Invoke-WebRequest -Uri $Uri -OutFile $OutFile -UseBasicParsing
}

$Tag = $env:CFIRE_VERSION
if (-not $Tag) {
    $LatestRedirectUri = if ($env:CFIRE_LATEST_RELEASE_URL) {
        $env:CFIRE_LATEST_RELEASE_URL
    } else {
        "https://github.com/gsandoval/cfire/releases/latest"
    }
    try {
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    } catch {}
    try {
        $Req = [System.Net.HttpWebRequest]::Create($LatestRedirectUri)
        $Req.Method = "HEAD"
        $Req.AllowAutoRedirect = $false
        $Req.UserAgent = "cfire-installer"
        $Loc = $null
        try {
            $Resp = $Req.GetResponse()
            $Loc = $Resp.Headers["Location"]
            $Resp.Close()
        } catch [System.Net.WebException] {
            if ($_.Exception.Response) {
                $Loc = $_.Exception.Response.Headers["Location"]
                $_.Exception.Response.Close()
            } else {
                throw
            }
        }
        if ($Loc -and $Loc -match '/releases/tag/([^/?#]+)') {
            $Tag = $Matches[1]
        }
    } catch {
        $Tag = $null
    }
    if (-not $Tag) {
        $LatestUri = "https://api.github.com/repos/gsandoval/cfire/releases/latest"
        $ReleaseInfo = Invoke-RestMethod -Uri $LatestUri -Headers @{ "User-Agent" = "cfire-installer" }
        $Tag = $ReleaseInfo.tag_name
    }
}
if (-not $Tag) {
    throw "Could not resolve latest cfire release version. Set `$env:CFIRE_VERSION = 'v0.11.0' and retry."
}
if (-not $Tag.StartsWith("v")) {
    $Tag = "v$Tag"
}

$BaseUrl = if ($env:CFIRE_RELEASE_BASE_URL) {
    $env:CFIRE_RELEASE_BASE_URL.TrimEnd('/')
} else {
    "https://github.com/gsandoval/cfire/releases/download/$Tag"
}

$InstallDir = if ($env:CFIRE_INSTALL_DIR) {
    $env:CFIRE_INSTALL_DIR
} elseif ($env:CFIRE_INSTALL_PREFIX) {
    $env:CFIRE_INSTALL_PREFIX
} else {
    Join-Path $env:LOCALAPPDATA "Programs\cfire"
}

$BundleName = "cfire-$Tag-$TargetArch"
$ArchiveName = "$BundleName.zip"
$ArchiveUrl = "$BaseUrl/$ArchiveName"
$SumsUrl = "$BaseUrl/SHA256SUMS.txt"

Write-Host "  - Version: $Tag ($TargetArch)" -ForegroundColor Cyan
Write-Host "  - Dest:    $InstallDir" -ForegroundColor Cyan

$TempDir = Join-Path ([System.IO.Path]::GetTempPath()) ("cfire_install_" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $TempDir -Force | Out-Null

try {
    $ArchivePath = Join-Path $TempDir $ArchiveName
    $SumsPath = Join-Path $TempDir "SHA256SUMS.txt"

    Write-Host "==> Downloading $ArchiveName and SHA256SUMS.txt..."
    Download-CfireFile -Uri $ArchiveUrl -OutFile $ArchivePath
    Download-CfireFile -Uri $SumsUrl -OutFile $SumsPath

    $ExpectedHash = $null
    foreach ($Line in (Get-Content -Path $SumsPath)) {
        if ($Line -match '^\s*([A-Fa-f0-9]{64})\s+\*?\.?/?(.+?)\s*$') {
            $EntryHash = $Matches[1].ToLowerInvariant()
            $EntryName = $Matches[2].Trim()
            if ($EntryName -eq $ArchiveName) {
                $ExpectedHash = $EntryHash
                break
            }
        }
    }
    if (-not $ExpectedHash) {
        throw "Checksum entry for '$ArchiveName' was not found in SHA256SUMS.txt."
    }

    $ActualHash = (Get-FileHash -Path $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ActualHash -ne $ExpectedHash) {
        throw "SHA-256 verification failed for $ArchiveName (expected $ExpectedHash, got $ActualHash)."
    }
    Write-Host "  OK SHA-256 verified ($ActualHash)" -ForegroundColor Green

    $UnpackDir = Join-Path $TempDir "unpack"
    New-Item -ItemType Directory -Path $UnpackDir -Force | Out-Null
    Expand-Archive -Path $ArchivePath -DestinationPath $UnpackDir -Force

    $BundleRoot = Join-Path $UnpackDir $BundleName
    if (-not (Test-Path (Join-Path $BundleRoot "cfire.exe"))) {
        $Found = Get-ChildItem -Path $UnpackDir -Filter "cfire.exe" -Recurse | Select-Object -First 1
        if (-not $Found) {
            throw "Downloaded archive did not contain cfire.exe."
        }
        $BundleRoot = $Found.Directory.FullName
    }

    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
    $DestExe = Join-Path $InstallDir "cfire.exe"
    $TmpExe = Join-Path $InstallDir (".cfire.exe.tmp." + $PID)
    $OldExe = Join-Path $InstallDir "cfire.exe.old"
    Copy-Item -Path (Join-Path $BundleRoot "cfire.exe") -Destination $TmpExe -Force
    if (Test-Path $OldExe) { Remove-Item -Path $OldExe -Force -ErrorAction SilentlyContinue }
    $MovedOldExe = $false
    try {
        if (Test-Path $DestExe) {
            Move-Item -Path $DestExe -Destination $OldExe -Force
            $MovedOldExe = $true
        }
        Move-Item -Path $TmpExe -Destination $DestExe -Force
    } catch {
        if ($MovedOldExe -and (Test-Path $OldExe) -and -not (Test-Path $DestExe)) {
            Move-Item -Path $OldExe -Destination $DestExe -Force -ErrorAction SilentlyContinue
        }
        throw
    }
    if (Test-Path $OldExe) { Remove-Item -Path $OldExe -Force -ErrorAction SilentlyContinue }

    if (Test-Path (Join-Path $BundleRoot "runtime")) {
        $DestRt = Join-Path $InstallDir "runtime"
        $StageRt = Join-Path $InstallDir (".runtime.tmp." + $PID)
        $OldRt = Join-Path $InstallDir (".runtime.old." + $PID)
        if (Test-Path $StageRt) { Remove-Item -Path $StageRt -Recurse -Force }
        if (Test-Path $OldRt) { Remove-Item -Path $OldRt -Recurse -Force }
        Copy-Item -Path (Join-Path $BundleRoot "runtime") -Destination $StageRt -Recurse -Force
        $OldVerFile = Join-Path $StageRt "VERSION"
        if (Test-Path $OldVerFile) { Remove-Item -Path $OldVerFile -Force }
        $MovedOldRt = $false
        try {
            if (Test-Path $DestRt) {
                Move-Item -Path $DestRt -Destination $OldRt -Force
                $MovedOldRt = $true
            }
            Move-Item -Path $StageRt -Destination $DestRt -Force
        } catch {
            if ($MovedOldRt -and (Test-Path $OldRt) -and -not (Test-Path $DestRt)) {
                Move-Item -Path $OldRt -Destination $DestRt -Force -ErrorAction SilentlyContinue
            }
            throw
        }
        if (Test-Path $OldRt) { Remove-Item -Path $OldRt -Recurse -Force -ErrorAction SilentlyContinue }
    }

    if ($env:CFIRE_NO_MODIFY_PATH -ne '1') {
        $UserPath = [Environment]::GetEnvironmentVariable("Path", "User")
        $PathParts = @()
        if ($UserPath) {
            $PathParts = $UserPath -split ';' | Where-Object { $_ -ne '' }
        }
        if ($PathParts -notcontains $InstallDir) {
            $NewPath = ($PathParts + $InstallDir) -join ';'
            [Environment]::SetEnvironmentVariable("Path", $NewPath, "User")
            $env:Path = "$env:Path;$InstallDir"
            Write-Host "Added $InstallDir to your User PATH. Open a new terminal to use 'cfire' everywhere." -ForegroundColor Yellow
        }
    }

    $ExePath = Join-Path $InstallDir "cfire.exe"
    & $ExePath --version
    if (-not (Get-Command clang++ -ErrorAction SilentlyContinue)) {
        Write-Host ""
        Write-Host "Note: 'clang++' was not found on PATH." -ForegroundColor Yellow
        Write-Host "Install LLVM/Clang or MSYS2 CLANG64 so cfire can compile '.cf' programs:"
        Write-Host "  winget install LLVM.LLVM"
    }
    if ($Setup -or $env:CFIRE_SETUP -eq '1') {
        & $ExePath setup --yes
    }
    Write-Host ""
    Write-Host "Installed cfire to $ExePath" -ForegroundColor Green
} finally {
    if (Test-Path $TempDir) {
        Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
