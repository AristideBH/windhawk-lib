# Sidebar probe: does ReadDirectoryChangesW report a write to a named stream, and under what name?
# Watches a temp folder tree with the stream filters, writes ":ExplorerExtras.Labels" on a file in a
# sub folder, deletes it, and prints every notification.
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;

public static class StreamWatch {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    static extern IntPtr CreateFileW(string n, uint acc, uint share, IntPtr sec, uint disp, uint flags, IntPtr t);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadDirectoryChangesW(IntPtr h, byte[] buf, int len, bool sub, uint filter, out uint ret, IntPtr ov, IntPtr cr);
    [DllImport("kernel32.dll")] static extern bool CancelIoEx(IntPtr h, IntPtr ov);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);

    public static List<string> Run(string dir, Action act, int ms) {
        var seen = new List<string>();
        IntPtr h = CreateFileW(dir, 0x1, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero); // list directory, backup semantics
        var t = new Thread(() => {
            var buf = new byte[65536];
            while (true) {
                uint ret;
                // STREAM_NAME 0x200 | STREAM_SIZE 0x400 | STREAM_WRITE 0x800, plus file name and last write for reference
                if (!ReadDirectoryChangesW(h, buf, buf.Length, true, 0x200 | 0x400 | 0x800 | 0x1 | 0x10, out ret, IntPtr.Zero, IntPtr.Zero) || ret == 0) break;
                int pos = 0;
                while (true) {
                    int next = BitConverter.ToInt32(buf, pos);
                    int action = BitConverter.ToInt32(buf, pos + 4);
                    int len = BitConverter.ToInt32(buf, pos + 8);
                    string name = System.Text.Encoding.Unicode.GetString(buf, pos + 12, len);
                    lock (seen) seen.Add(action + " " + name);
                    if (next == 0) break;
                    pos += next;
                }
            }
        });
        t.Start();
        Thread.Sleep(300);
        act();
        Thread.Sleep(ms);
        CancelIoEx(h, IntPtr.Zero);
        t.Join(2000);
        CloseHandle(h);
        lock (seen) return new List<string>(seen);
    }
}
'@

$root = Join-Path $env:TEMP 'extras-streamwatch'
Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$root\sub" | Out-Null
Set-Content "$root\sub\a.txt" 'hello'
Start-Sleep -Milliseconds 300

Write-Host '--- write the stream'
$r = [StreamWatch]::Run($root, [Action]{ Set-Content "$root\sub\a.txt" -Stream 'ExplorerExtras.Labels' -Value "v1`n1`tUrgent" }, 1500)
$r
Write-Host '--- change the stream'
$r = [StreamWatch]::Run($root, [Action]{ Set-Content "$root\sub\a.txt" -Stream 'ExplorerExtras.Labels' -Value "v1`n2`tLater" }, 1500)
$r
Write-Host '--- remove the stream'
$r = [StreamWatch]::Run($root, [Action]{ Remove-Item "$root\sub\a.txt" -Stream 'ExplorerExtras.Labels' }, 1500)
$r
Remove-Item $root -Recurse -Force
Write-Host '--- DONE'
