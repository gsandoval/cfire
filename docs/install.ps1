# cfire installer for Windows (PowerShell 5.1+ / 7+)
# Usage:
#   irm https://cfire.memo.engineer/install.ps1 | iex
# Optional environment variables:
#   $env:CFIRE_VERSION          - Specific tag (e.g. "v0.16.0")
#   $env:CFIRE_INSTALL_DIR      - Custom install directory (default: $env:LOCALAPPDATA\Programs\cfire)
#   $env:CFIRE_RELEASE_BASE_URL - Custom mirror or local file:// release base URL

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

$Tag = $env:CFIRE_VERSION
if (-not $Tag) {
    $LatestUri = "https://api.github.com/repos/gsandoval/cfire/releases/latest"
    $ReleaseInfo = Invoke-RestMethod -Uri $LatestUri -Headers @{ "User-Agent" = "cfire-installer" }
    $Tag = $ReleaseInfo.tag_name
}
if (-not $Tag) {
    throw "Could not resolve latest cfire release version. Set `$env:CFIRE_VERSION = 'v0.16.0' and retry."
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
    Invoke-WebRequest -Uri $ArchiveUrl -OutFile $ArchivePath -UseBasicParsing
    Invoke-WebRequest -Uri $SumsUrl -OutFile $SumsPath -UseBasicParsing

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
    Copy-Item -Path (Join-Path $BundleRoot "cfire.exe") -Destination (Join-Path $InstallDir "cfire.exe") -Force
    if (Test-Path (Join-Path $BundleRoot "runtime")) {
        $DestRt = Join-Path $InstallDir "runtime"
        if (Test-Path $DestRt) { Remove-Item -Path $DestRt -Recurse -Force }
        Copy-Item -Path (Join-Path $BundleRoot "runtime") -Destination $DestRt -Recurse -Force
        $OldVerFile = Join-Path $DestRt "VERSION"
        if (Test-Path $OldVerFile) { Remove-Item -Path $OldVerFile -Force }
    }
    if (Test-Path (Join-Path $BundleRoot "pkg")) {
        $DestPkg = Join-Path $InstallDir "pkg"
        if (Test-Path $DestPkg) { Remove-Item -Path $DestPkg -Recurse -Force }
        Copy-Item -Path (Join-Path $BundleRoot "pkg") -Destination $DestPkg -Recurse -Force
    }

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

    $ExePath = Join-Path $InstallDir "cfire.exe"
    & $ExePath --version
    if (-not (Get-Command clang++ -ErrorAction SilentlyContinue)) {
        Write-Host ""
        Write-Host "Note: 'clang++' was not found on PATH." -ForegroundColor Yellow
        Write-Host "Install LLVM/Clang or MSYS2 CLANG64 so cfire can compile '.cf' programs:"
        Write-Host "  winget install LLVM.LLVM"
    }
    Write-Host ""
    Write-Host "Installed cfire to $ExePath" -ForegroundColor Green
} finally {
    if (Test-Path $TempDir) {
        Remove-Item -Path $TempDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}
