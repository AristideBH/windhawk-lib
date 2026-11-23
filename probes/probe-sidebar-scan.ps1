# Sidebar probe: how long does a background scan of a fixed drive for the
# ":ExplorerExtras.Labels" stream take, and what does it cost?
# Same walk the tag index would do: background I/O priority, no reparse points,
# the excluded folders skipped, one open attempt on the named stream per file.
param(
    [string]$Root = 'C:\',
    [int]$Seconds = 240
)

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Threading;

public static class StreamScan {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct WIN32_FIND_DATA {
        public uint attrs; public uint c0, c1, a0, a1, w0, w1; public uint sizeHigh, sizeLow, r0, r1;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string name;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 14)] public string alt;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr FindFirstFileExW(string f, int level, out WIN32_FIND_DATA d, int op, IntPtr g, int flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    static extern bool FindNextFileW(IntPtr h, out WIN32_FIND_DATA d);
    [DllImport("kernel32.dll")] static extern bool FindClose(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateFileW(string n, uint acc, uint share, IntPtr sec, uint disp, uint flags, IntPtr t);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll")] static extern bool SetThreadPriority(IntPtr t, int p);
    [DllImport("kernel32.dll")] static extern IntPtr GetCurrentThread();

    public static long Files, Dirs, Tagged, Errors, Skipped;
    static readonly string[] Excluded = { "windows", "program files", "program files (x86)", "programdata", "$recycle.bin", "system volume information" };

    public static string Run(string root, int seconds, Action<string> log) {
        SetThreadPriority(GetCurrentThread(), 0x00010000); // THREAD_MODE_BACKGROUND_BEGIN
        var sw = Stopwatch.StartNew();
        var stack = new Stack<string>();
        stack.Push(root.TrimEnd('\\'));
        long lastLog = 0;
        bool finished = true;
        while (stack.Count > 0) {
            if (sw.Elapsed.TotalSeconds > seconds) { finished = false; break; }
            string dir = stack.Pop();
            WIN32_FIND_DATA d;
            IntPtr h = FindFirstFileExW(dir + "\\*", 1 /*basic*/, out d, 0, IntPtr.Zero, 2 /*large fetch*/);
            if (h == new IntPtr(-1)) { Errors++; continue; }
            Dirs++;
            do {
                if (d.name == "." || d.name == "..") continue;
                string full = dir + "\\" + d.name;
                bool isDir = (d.attrs & 0x10) != 0;
                if ((d.attrs & 0x400) != 0) { Skipped++; continue; }              // reparse point
                if (isDir) {
                    if (dir.Length <= 2 && Array.IndexOf(Excluded, d.name.ToLowerInvariant()) >= 0) { Skipped++; continue; }
                    stack.Push(full);
                } else {
                    if ((d.attrs & 0x40000) != 0 || (d.attrs & 0x400000) != 0) { Skipped++; continue; } // recall on open / data access
                    Files++;
                    IntPtr f = CreateFileW(full + ":ExplorerExtras.Labels", 0x80000000, 7, IntPtr.Zero, 3, 0, IntPtr.Zero);
                    if (f != new IntPtr(-1)) { Tagged++; CloseHandle(f); }
                }
            } while (FindNextFileW(h, out d));
            FindClose(h);
            if (sw.ElapsedMilliseconds - lastLog > 30000) {
                lastLog = sw.ElapsedMilliseconds;
                log(string.Format("{0}s files={1} dirs={2} tagged={3} errors={4} skipped={5}", (int)sw.Elapsed.TotalSeconds, Files, Dirs, Tagged, Errors, Skipped));
            }
        }
        return string.Format("{0} after {1:F1}s: files={2} dirs={3} tagged={4} errors={5} skipped={6} rate={7:F0} files/s",
            finished ? "FINISHED" : "STOPPED at the limit", sw.Elapsed.TotalSeconds, Files, Dirs, Tagged, Errors, Skipped, Files / Math.Max(1, sw.Elapsed.TotalSeconds));
    }
}
'@

$p = Get-Process -Id $PID
$ws0 = $p.WorkingSet64
Write-Host "scan $Root for $Seconds s at background priority"
$result = [StreamScan]::Run($Root, $Seconds, [Action[string]]{ param($m) Write-Host $m })
Write-Host $result
$p.Refresh()
Write-Host ("working set grew by {0:N0} MB" -f (($p.WorkingSet64 - $ws0) / 1MB))
