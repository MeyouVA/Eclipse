param([switch]$Install, [ValidateSet('auto','x64','arm64')][string]$Architecture = 'auto')

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function File-Hash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose(); $stream.Dispose() }
}

function Get-TreeRecord([string]$Directory) {
    $prefix = $Directory.TrimEnd('\') + '\'
    @(Get-ChildItem -LiteralPath $Directory -File -Recurse | Where-Object { $_.Name -ne '.installed.json' } | ForEach-Object {
        [pscustomobject]@{ path = $_.FullName.Substring($prefix.Length); bytes = $_.Length; sha256 = (File-Hash $_.FullName) }
    })
}

try {
    $native = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' -Name PROCESSOR_ARCHITECTURE).PROCESSOR_ARCHITECTURE
    if ($Architecture -eq 'auto') {
        $Architecture = switch ($native.ToUpperInvariant()) {
            'AMD64' { 'x64' }
            'ARM64' { 'arm64' }
            default { throw "Unsupported Windows architecture: $native. Eclipse needs 64-bit Windows." }
        }
    }
    Write-Host "Native CPU architecture: $native; selected packages: $Architecture (Intel and AMD both use x64)."
    $manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'thirdparty.json') -Raw | ConvertFrom-Json
    if ($Architecture -eq 'arm64') {
        foreach ($package in $manifest) {
            if ($package.source) { continue }
            if (-not $package.arm64) { throw "No pinned ARM64 package is available for $($package.name). Nothing downloaded." }
            foreach ($key in @('url','bytes','sha256','archive')) { $package.$key = $package.arm64.$key }
        }
    }
    $manifest | Select-Object name, version, license, bytes, url | Format-List | Out-Host
    if (-not $Install) {
        Write-Host 'Plan only. Nothing downloaded. After approval, run scripts\install_thirdparty.bat -Install.'
        exit 0
    }
    $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $root = Join-Path $repo 'thirdparty'
    if ((Test-Path -LiteralPath $root) -and ((Get-Item -LiteralPath $root).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'thirdparty must not be a reparse point.' }
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $downloadRoot = Join-Path $root '.downloads'
    if ((Test-Path -LiteralPath $downloadRoot) -and ((Get-Item -LiteralPath $downloadRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'The download directory must not be a reparse point.' }
    New-Item -ItemType Directory -Path $downloadRoot -Force | Out-Null
    $tar = (Get-Command tar.exe -ErrorAction Stop).Source
    $sevenZip = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
    if (-not (Test-Path -LiteralPath $sevenZip)) {
        $available = Get-Command 7z.exe -ErrorAction SilentlyContinue
        $sevenZip = if ($available) { $available.Source } else { '' }
    }
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    foreach ($package in $manifest) {
        if ($package.name -notmatch '^[a-z0-9-]+$' -or $package.archive -notmatch '^[a-zA-Z0-9._-]+$' -or $package.sha256 -notmatch '^[0-9a-f]{64}$') { throw 'Invalid package manifest.' }
        $destination = Join-Path $root $package.name
        if (Test-Path -LiteralPath $destination) {
            if ((Get-Item -LiteralPath $destination).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Unsafe package directory: $destination" }
            $recordPath = Join-Path $destination '.installed.json'
            if (-not (Test-Path -LiteralPath $recordPath)) { throw "Existing $destination has no verified installation record; inspect it before installing." }
            $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
            $installedArchitecture = if ($record.architecture) { $record.architecture } else { 'x64' }
            if (-not $package.source -and $installedArchitecture -ne $Architecture) { throw "Installed $($package.name) is $installedArchitecture, but $Architecture was selected. Inspect it before replacing." }
            if ($record.archiveSha256 -ne $package.sha256) { throw "Installed $($package.name) has another version; inspect it before replacing." }
            foreach ($entry in $record.files) {
                $installedFile = [IO.Path]::GetFullPath((Join-Path $destination $entry.path))
                if (-not $installedFile.StartsWith($destination + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid installed-file record.' }
                if (-not (Test-Path -LiteralPath $installedFile -PathType Leaf) -or (File-Hash $installedFile) -ne $entry.sha256) { throw "Installed file changed or missing: $installedFile" }
            }
            Write-Host "Verified; skipping $($package.name) $($package.version)."
            continue
        }
        $archive = Join-Path $downloadRoot $package.archive
        if (-not (Test-Path -LiteralPath $archive)) {
            Write-Host "Downloading $($package.name) ($($package.bytes) bytes)..."
            Invoke-WebRequest -UseBasicParsing -Uri $package.url -OutFile $archive
        }
        if ((Get-Item -LiteralPath $archive).Length -ne $package.bytes -or (File-Hash $archive) -ne $package.sha256) { throw "Archive size or SHA-256 mismatch: $archive. No files installed; inspect the archive before retrying." }
        $use7z = $package.archive.EndsWith('.7z')
        if ($use7z -and -not $sevenZip) { throw 'A local 7-Zip installation is required to extract .7z packages. No extractor is downloaded automatically.' }
        if ($use7z) { $entries = @(& $sevenZip l -slt -ba $archive | Where-Object { $_.StartsWith('Path = ') } | ForEach-Object { $_.Substring(7) }) }
        else { $entries = @(& $tar -tf $archive) }
        if ($LASTEXITCODE -ne 0) { throw "Cannot list archive: $archive" }
        foreach ($entry in $entries) {
            if ($entry -match '(^[/\\]|^[a-zA-Z]:|(^|[/\\])\.\.([/\\]|$))') { throw "Unsafe archive member: $entry" }
        }
        $staging = Join-Path $root ('.install-' + $package.name + '-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $staging | Out-Null
        if ($use7z) { & $sevenZip x -y "-o$staging" $archive | Out-Null }
        else { & $tar -xf $archive -C $staging }
        if ($LASTEXITCODE -ne 0) { throw "Extraction failed for $archive. Staging left at $staging for inspection." }
        if (Get-ChildItem -LiteralPath $staging -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw "Extracted package contains links; inspect $staging." }
        $payload = $staging
        $top = @(Get-ChildItem -LiteralPath $staging -Force)
        if ($top.Count -eq 1 -and $top[0].PSIsContainer) { $payload = $top[0].FullName }
        $files = @(Get-TreeRecord $payload)
        if ($files.Count -eq 0) { throw "Empty package: $archive" }
        @{ archiveSha256 = $package.sha256; version = $package.version; architecture = $Architecture; files = $files } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $payload '.installed.json') -Encoding UTF8
        if (-not ([IO.Path]::GetFullPath($payload)).StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase) -or -not ([IO.Path]::GetFullPath($destination)).StartsWith($root + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Installation paths must stay within thirdparty.' }
        Move-Item -LiteralPath $payload -Destination $destination
        if ($payload -ne $staging) { Remove-Item -LiteralPath $staging }
        Write-Host "Installed $($package.name) $($package.version)."
    }
    $notices = @('# Third-party software', '', 'Keep the original license, copyright and notice files shipped inside every package when packaging runtime files.', '')
    foreach ($package in $manifest) {
        $notices += @("## $($package.name) $($package.version)", '', $package.license, '', "Used for: $($package.purpose)", '', "Source: $($package.url)", '', "Archive SHA-256: $($package.sha256)", '')
        foreach ($noticeName in @('NOTICE.txt','LICENSE.txt','COPYING','COPYING.LESSER')) {
            $noticePath = Join-Path (Join-Path $root $package.name) $noticeName
            if (Test-Path -LiteralPath $noticePath) {
                $notices += @("### Original $noticeName", '', (Get-Content -LiteralPath $noticePath -Raw), '')
            }
        }
    }
    $notices | Set-Content -LiteralPath (Join-Path $root 'LICENSES.md') -Encoding UTF8
    if ((Test-Path -LiteralPath (Join-Path $root 'libheif\CMakeLists.txt')) -and (Test-Path -LiteralPath (Join-Path $root 'x265\source\CMakeLists.txt'))) {
        & $env:ComSpec /c (Join-Path $PSScriptRoot 'build_image_codecs.bat')
        if ($LASTEXITCODE -ne 0) { throw 'HEIC codec build failed. Sources are retained for inspection; no extra packages were downloaded.' }
    }
    Write-Host 'Installation complete. Qt is checked separately by scripts\findtools.bat.'
    exit 0
} catch {
    Write-Error "Third-party installation failed: $($_.Exception.Message)" -ErrorAction Continue
    exit 1
}
