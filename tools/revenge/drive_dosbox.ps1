param(
    [Parameter(Mandatory = $true)]
    [int]$ProcessId,

    [string[]]$Keys = @(),

    [string]$CapturePath = "",

    [int]$KeyDelayMilliseconds = 90,

    [int]$SettleMilliseconds = 500
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class RevengeDosboxWindow {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr p);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr h);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int command);
    [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
}
'@

$process = Get-Process -Id $ProcessId
$window = $process.MainWindowHandle
if ($window -eq [IntPtr]::Zero) {
    throw "Process $ProcessId has no main window."
}

$foreground = [RevengeDosboxWindow]::GetForegroundWindow()
$currentThread = [RevengeDosboxWindow]::GetCurrentThreadId()
$foregroundThread = [RevengeDosboxWindow]::GetWindowThreadProcessId($foreground, [IntPtr]::Zero)
$targetThread = [RevengeDosboxWindow]::GetWindowThreadProcessId($window, [IntPtr]::Zero)
[RevengeDosboxWindow]::AttachThreadInput($currentThread, $foregroundThread, $true) | Out-Null
[RevengeDosboxWindow]::AttachThreadInput($currentThread, $targetThread, $true) | Out-Null
[RevengeDosboxWindow]::ShowWindow($window, 9) | Out-Null
[RevengeDosboxWindow]::BringWindowToTop($window) | Out-Null
[RevengeDosboxWindow]::SetForegroundWindow($window) | Out-Null
[RevengeDosboxWindow]::SetFocus($window) | Out-Null
[RevengeDosboxWindow]::AttachThreadInput($currentThread, $targetThread, $false) | Out-Null
[RevengeDosboxWindow]::AttachThreadInput($currentThread, $foregroundThread, $false) | Out-Null

$namedKeys = @{
    SPACE = 0x20; ENTER = 0x0D; RETURN = 0x0D; ESC = 0x1B; ESCAPE = 0x1B
    UP = 0x26; DOWN = 0x28; LEFT = 0x25; RIGHT = 0x27; TAB = 0x09
    BACKSPACE = 0x08; DELETE = 0x2E
}
foreach ($keyName in $Keys) {
    $upper = $keyName.ToUpperInvariant()
    if ($namedKeys.ContainsKey($upper)) {
        $virtualKey = [byte]$namedKeys[$upper]
    } elseif ($keyName.Length -eq 1) {
        $virtualKey = [byte][char]$upper
    } else {
        throw "Unknown key '$keyName'. Use a single character or a named navigation key."
    }
    $scanCode = [byte][RevengeDosboxWindow]::MapVirtualKey($virtualKey, 0)
    [RevengeDosboxWindow]::keybd_event($virtualKey, $scanCode, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $KeyDelayMilliseconds
    [RevengeDosboxWindow]::keybd_event($virtualKey, $scanCode, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $KeyDelayMilliseconds
}

Start-Sleep -Milliseconds $SettleMilliseconds

if ($CapturePath) {
    $rect = New-Object RevengeDosboxWindow+RECT
    if (-not [RevengeDosboxWindow]::GetWindowRect($window, [ref]$rect)) {
        throw "GetWindowRect failed."
    }
    $bitmap = New-Object Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try {
        if (-not [RevengeDosboxWindow]::PrintWindow($window, $dc, 0)) {
            throw "PrintWindow failed."
        }
    } finally {
        $graphics.ReleaseHdc($dc)
        $graphics.Dispose()
    }
    $absoluteCapture = [IO.Path]::GetFullPath($CapturePath)
    $captureDirectory = Split-Path -Parent $absoluteCapture
    if ($captureDirectory -and -not (Test-Path -LiteralPath $captureDirectory)) {
        New-Item -ItemType Directory -Path $captureDirectory | Out-Null
    }
    $bitmap.Save($absoluteCapture, [Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Output $absoluteCapture
}
