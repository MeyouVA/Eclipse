#Requires -Version 5.1
[CmdletBinding()]
param([string] $Executable)

$ErrorActionPreference = 'Stop'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This command registers Windows Explorer file types.'
}
if (-not $Executable) {
    $Executable = Join-Path $PSScriptRoot 'Eclipse.exe'
    if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        $Executable = Join-Path $PSScriptRoot '..\out\Eclipse.exe'
    }
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw 'Could not find Eclipse.exe. Build Eclipse or pass -Executable with its full path.'
}
$Executable = (Resolve-Path -LiteralPath $Executable).ProviderPath
if ([IO.Path]::GetFileName($Executable) -ne 'Eclipse.exe') {
    throw 'Choose the Eclipse.exe file you want Explorer to launch.'
}

$types = @(
    @{ Extension = '.ecb'; ProgId = 'Eclipse.Storyboard'; Name = 'Eclipse Storyboard' },
    @{ Extension = '.ecbz'; ProgId = 'Eclipse.StoryboardPacked'; Name = 'Packed Eclipse Storyboard' }
)
$classes = [Microsoft.Win32.Registry]::ClassesRoot
foreach ($type in $types) {
    $extension = $classes.OpenSubKey($type.Extension)
    if ($extension) {
        try { $existing = $extension.GetValue('') } finally { $extension.Dispose() }
        if ($existing -and $existing -ne $type.ProgId) {
            throw ('Windows already has a handler for {0}. Choose Eclipse.exe using Open with in Explorer.' -f $type.Extension)
        }
    }
    $userChoice = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
        'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\' + $type.Extension + '\UserChoice')
    if ($userChoice) {
        try { $choice = $userChoice.GetValue('ProgId') } finally { $userChoice.Dispose() }
        if ($choice -and $choice -ne $type.ProgId) {
            throw ('Windows has a preferred app for {0}. Choose Eclipse.exe using Open with in Explorer.' -f $type.Extension)
        }
    }
}

if (-not ('EclipseFileAssociation.Shell' -as [type])) {
    Add-Type @'
using System.Runtime.InteropServices;

namespace EclipseFileAssociation {
    public static class Shell {
        [DllImport("shell32.dll")]
        public static extern void SHChangeNotify(int eventId, uint flags, System.IntPtr item1, System.IntPtr item2);
    }
}
'@
}

function Set-ClassDefault([string] $SubKey, [string] $Value) {
    $key = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Software\Classes\' + $SubKey)
    try {
        $key.SetValue('', $Value, [Microsoft.Win32.RegistryValueKind]::String)
    } finally {
        $key.Dispose()
    }
}

foreach ($type in $types) {
    Set-ClassDefault $type.ProgId $type.Name
    Set-ClassDefault "$($type.ProgId)\DefaultIcon" ('"{0}",0' -f $Executable)
    Set-ClassDefault "$($type.ProgId)\shell\open\command" ('"{0}" "%1"' -f $Executable)
    Set-ClassDefault $type.Extension $type.ProgId
}
[EclipseFileAssociation.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Output "Explorer now opens .ecb and .ecbz files with $Executable"
