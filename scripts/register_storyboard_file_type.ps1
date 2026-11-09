#Requires -Version 5.1
[CmdletBinding()]
param([string] $Executable)

$ErrorActionPreference = 'Stop'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This command registers a Windows Explorer file type.'
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

$progId = 'Eclipse.Storyboard'
$classes = [Microsoft.Win32.Registry]::ClassesRoot
$extension = $classes.OpenSubKey('.ecb')
if ($extension) {
    try { $existing = $extension.GetValue('') } finally { $extension.Dispose() }
    if ($existing -and $existing -ne $progId) {
        throw 'Windows already has a handler for .ecb. Choose Eclipse.exe using Open with in Explorer.'
    }
}
$userChoice = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
    'Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.ecb\UserChoice')
if ($userChoice) {
    try { $choice = $userChoice.GetValue('ProgId') } finally { $userChoice.Dispose() }
    if ($choice -and $choice -ne $progId) {
        throw 'Windows has a preferred app for .ecb. Choose Eclipse.exe using Open with in Explorer.'
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

Set-ClassDefault $progId 'Eclipse Storyboard'
Set-ClassDefault "$progId\DefaultIcon" ('"{0}",0' -f $Executable)
Set-ClassDefault "$progId\shell\open\command" ('"{0}" "%1"' -f $Executable)
Set-ClassDefault '.ecb' $progId
[EclipseFileAssociation.Shell]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)
Write-Output "Explorer now opens .ecb files with $Executable"
