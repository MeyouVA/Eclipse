param([Parameter(Mandatory=$true)][string]$Destination)

$ErrorActionPreference = 'Stop'
try {
    $repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
    $target = [IO.Path]::GetFullPath($Destination)
    if ($target -ne (Join-Path $repo 'Release.new') -and -not $target.StartsWith((Join-Path $repo 'out') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Package destination must be Release.new or a test folder inside out.' }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw 'Package destination does not exist.' }
    $walk = $target
    while ($walk.Length -ge $repo.Length) {
        if ((Get-Item -LiteralPath $walk).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Package path contains a reparse point: $walk" }
        if ($walk -eq $repo) { break }
        $walk = Split-Path -Parent $walk
    }
    $source = Join-Path $repo 'thirdparty'
    $runtime = Join-Path $target 'thirdparty'
    foreach ($relative in @('ffmpeg\bin\ffmpeg.exe','ffmpeg\bin\ffprobe.exe','imagemagick\magick.exe','python\python.exe','heif\bin\eclipse-heif.exe','heif\bin\heif.dll','heif\bin\libx265.dll','LICENSES.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $source $relative) -PathType Leaf)) { throw "Missing runtime: $relative. Run scripts\install_thirdparty.bat -Install after reviewing its plan." }
    }
    if (Get-ChildItem -LiteralPath $source -Recurse -Force | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) { throw 'Third-party packages must not contain reparse points.' }
    if ((Test-Path -LiteralPath $runtime) -and ((Get-Item -LiteralPath $runtime).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Runtime target must not be a reparse point.' }
    foreach ($package in @('ffmpeg','imagemagick','python','heif')) {
        $folder = Join-Path $runtime $package
        New-Item -ItemType Directory -Path $folder -Force | Out-Null
        if ($package -eq 'ffmpeg') {
            New-Item -ItemType Directory -Path (Join-Path $folder 'bin') -Force | Out-Null
            Get-ChildItem -LiteralPath (Join-Path $source 'ffmpeg\bin') -File | Where-Object { $_.Extension -eq '.dll' -or $_.Name -in @('ffmpeg.exe','ffprobe.exe') } | Copy-Item -Destination (Join-Path $folder 'bin') -Force
            Copy-Item -LiteralPath (Join-Path $source 'ffmpeg\LICENSE.txt') -Destination $folder -Force
            Copy-Item -LiteralPath (Join-Path $source 'ffmpeg\doc') -Destination $folder -Recurse -Force
        } elseif($package -eq 'heif') {
            Copy-Item -LiteralPath (Join-Path $source 'heif\bin') -Destination $folder -Recurse -Force
            Get-ChildItem -LiteralPath (Join-Path $source 'heif') -File | Where-Object { $_.Name -match 'COPYING|LICENSE|NOTICE' } | Copy-Item -Destination $folder -Force
        } else {
            Get-ChildItem -LiteralPath (Join-Path $source $package) -Force | Where-Object { $_.Name -ne '.installed.json' -and ($package -ne 'imagemagick' -or $_.Extension -ne '.exe' -or $_.Name -eq 'magick.exe') } | Copy-Item -Destination $folder -Recurse -Force
        }
    }
    Copy-Item -LiteralPath (Join-Path $source 'LICENSES.md') -Destination $runtime -Force
    Write-Host "Third-party runtime copied to $runtime"
    exit 0
} catch {
    Write-Error "Third-party packaging failed: $($_.Exception.Message)" -ErrorAction Continue
    exit 1
}
