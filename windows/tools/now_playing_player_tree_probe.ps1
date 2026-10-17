# Live shape of a window-title player's accessibility tree (read-only).
#
# Run it on your own machine, in your own desktop session, while the player
# (NetEase Cloud Music by default) is open:
#   powershell -ExecutionPolicy Bypass -File now_playing_player_tree_probe.ps1 [-Out tree.json] [-Image cloudmusic.exe]
# It finds the player's main window, the web page document inside it (a
# Chromium/CEF player exposes it only after Chromium's screen-reader probe,
# which this script sends once, exactly as EndfieldHUD's reader does), walks
# it the way the HUD does (MTA, MSAA with IAccessible2 object attributes) and
# writes the tree as JSON in the format of
# windows/tests/fixtures/now-playing-netease-tree.json. It also lists which of
# the media keys and the player's usual Ctrl+Alt shortcuts another program
# holds as global hotkeys (registered for an instant and released at once).
#
# Privacy: no label is written except control words (play, pause, previous,
# next, Chinese and English), clock texts (1:23) and progress-bar names;
# every other name, description and image source becomes "<N chars>". The
# window title is never written. Nothing is pressed, typed or changed; the
# probe turns on the player's accessibility tree until the player exits.
param([string]$Out = 'now-playing-player-tree.json', [string]$Image = 'cloudmusic.exe', [string]$Class = 'OrpheusBrowserHost')
$ErrorActionPreference = 'Stop'
Add-Type -ReferencedAssemblies Accessibility -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using Accessibility;

public static class NowPlayingTreeProbe {
    delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", EntryPoint = "GetWindowLongPtrW")] static extern IntPtr GetWindowLongPtr(IntPtr h, int i);
    [DllImport("user32.dll")] static extern IntPtr SendMessageTimeoutW(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll", SetLastError = true)] static extern bool RegisterHotKey(IntPtr h, int id, uint mod, uint vk);
    [DllImport("user32.dll")] static extern bool UnregisterHotKey(IntPtr h, int id);
    [DllImport("oleacc.dll")] static extern int AccessibleObjectFromWindow(IntPtr h, uint id, ref Guid iid, [MarshalAs(UnmanagedType.Interface)] out object o);
    [DllImport("oleacc.dll")] static extern int AccessibleChildren(IAccessible acc, int start, int count, [Out, MarshalAs(UnmanagedType.LPArray, SizeParamIndex = 4)] object[] children, out int obtained);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, uint pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern bool QueryFullProcessImageNameW(IntPtr h, uint flags, StringBuilder s, ref uint n);
    [ComImport, Guid("6d5140c1-7436-11ce-8034-00aa006009fa"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IServiceProvider { [PreserveSig] int QueryService(ref Guid sid, ref Guid iid, out IntPtr obj); }
    [UnmanagedFunctionPointer(CallingConvention.StdCall)] delegate int Ia2Attributes(IntPtr self, out IntPtr bstr);
    static readonly Guid IID_IAccessible = new Guid("618736e0-3c3d-11cf-810c-00aa00389b71");
    static readonly Guid IID_IAccessible2 = new Guid("E89F726E-C4F4-4C19-BB19-B647D7FA8478");
    static readonly Regex Clock = new Regex(@"^[\s/]*\d{1,3}(:\d{1,3}){1,2}[\s/]*(\d{1,3}(:\d{1,3}){1,2}[\s/]*)?$");
    static readonly Regex Word = new Regex(@"^(play|pause|pre|prev|previous|next|\u64ad\u653e|\u6682\u505c|\u66ab\u505c|\u4e0a\u4e00\u9996|\u4e0b\u4e00\u9996|\u4e0a\u4e00\u4e2a|\u4e0b\u4e00\u4e2a)(\s*[\(\uff08].*[\)\uff09])?$", RegexOptions.IgnoreCase);
    static readonly Regex Progress = new Regex(@"\u8fdb\u5ea6|\u9032\u5ea6|progress", RegexOptions.IgnoreCase);
    public static StringBuilder Json = new StringBuilder();
    static string Escape(string s) { var b = new StringBuilder(); foreach (var c in s) { if (c == '"' || c == '\\') b.Append('\\').Append(c); else if (c < 32) b.AppendFormat("\\u{0:x4}", (int)c); else b.Append(c); } return b.ToString(); }
    static string Safe(string s) {
        if (string.IsNullOrEmpty(s)) return "";
        var one = s.Replace("\r", " ").Replace("\n", " ");
        if (Clock.IsMatch(one) || Word.IsMatch(one.Trim()) || Progress.IsMatch(one)) return one;
        return "<" + s.Length + " chars>";
    }
    static string SafeAttributes(string a) {
        // class tokens and tags are page structure; any image source or id is redacted.
        var parts = new List<string>();
        foreach (Match m in Regex.Matches(a ?? "", @"((?:\\.|[^:;\\])*):((?:\\.|[^;\\])*);")) {
            var name = m.Groups[1].Value; var value = m.Groups[2].Value;
            if (name == "src" || name == "id" || name == "href") value = "<" + value.Length + " chars>";
            parts.Add(name + ":" + value + ";");
        }
        return string.Concat(parts);
    }
    static string Ia2(IAccessible a) {
        var sp = a as IServiceProvider; if (sp == null) return "";
        Guid s = IID_IAccessible, i = IID_IAccessible2; IntPtr p;
        if (sp.QueryService(ref s, ref i, out p) < 0 || p == IntPtr.Zero) return "";
        try {
            var attrs = (Ia2Attributes)Marshal.GetDelegateForFunctionPointer(Marshal.ReadIntPtr(Marshal.ReadIntPtr(p), 45 * IntPtr.Size), typeof(Ia2Attributes));
            IntPtr b; attrs(p, out b); var text = b == IntPtr.Zero ? "" : Marshal.PtrToStringBSTR(b); if (b != IntPtr.Zero) Marshal.FreeBSTR(b); return text;
        } finally { Marshal.Release(p); }
    }
    static string Cls(IntPtr h) { var b = new StringBuilder(64); GetClassNameW(h, b, 64); return b.ToString(); }
    static string Path(uint pid) { IntPtr p = OpenProcess(0x1000, false, pid); if (p == IntPtr.Zero) return ""; var b = new StringBuilder(1024); uint n = 1024; QueryFullProcessImageNameW(p, 0, b, ref n); CloseHandle(p); return b.ToString(); }
    static IAccessible Client(IntPtr h) { var iid = IID_IAccessible; object o; return AccessibleObjectFromWindow(h, 0xFFFFFFFC, ref iid, out o) < 0 ? null : o as IAccessible; }
    static int Kids(IAccessible a) { try { return a.accChildCount; } catch { return -1; } }
    static List<IAccessible> Children(IAccessible a) {
        var o = new List<IAccessible>(); int n = Kids(a); if (n <= 0) return o;
        var arr = new object[n]; int got; if (AccessibleChildren(a, 0, n, arr, out got) < 0) return o;
        for (int k = 0; k < got; ++k) if (arr[k] is IAccessible) o.Add((IAccessible)arr[k]);
        return o;
    }
    static int Role(IAccessible a) { try { object r = a.get_accRole(0); return r is int ? (int)r : 0; } catch { return 0; } }
    static string RoleName(int r) { return r == 0xF ? "document" : r == 0x2B || r == 0x38 || r == 0x39 || r == 0x3D ? "button" : r == 0x28 ? "image" : r == 0x29 ? "text" : r == 0x33 ? "slider" : "group"; }
    public static string Run(string image, string cls) {
        var tops = new List<IntPtr>(); EnumWindows((h, l) => { tops.Add(h); return true; }, IntPtr.Zero);
        IntPtr main = IntPtr.Zero;
        foreach (var h in tops) { uint pid; GetWindowThreadProcessId(h, out pid); if (Cls(h) == cls && Path(pid).EndsWith("\\" + image, StringComparison.OrdinalIgnoreCase)) { main = h; break; } }
        if (main == IntPtr.Zero) return "{\"error\":\"no main window\"}";
        var hosts = new List<IntPtr>();
        EnumChildWindows(main, (h, l) => { var c = Cls(h); if ((GetWindowLongPtr(h, -16).ToInt64() & 0x10000000) != 0 && (c == "Chrome_RenderWidgetHostHWND" || c.StartsWith("Chrome_WidgetWin_"))) hosts.Add(h); return true; }, IntPtr.Zero);
        var hot = new StringBuilder();
        foreach (var k in new[] { new[] { 0, 0xB3 }, new[] { 0, 0xB0 }, new[] { 0, 0xB1 }, new[] { 3, 0x50 }, new[] { 3, 0x25 }, new[] { 3, 0x27 } }) {
            bool free = RegisterHotKey(IntPtr.Zero, 0x7E31, (uint)k[0] | 0x4000, (uint)k[1]); int err = Marshal.GetLastWin32Error(); if (free) UnregisterHotKey(IntPtr.Zero, 0x7E31);
            hot.AppendFormat("{0}\"mod{1}vk{2:X2}\":\"{3}\"", hot.Length > 0 ? "," : "", k[0], k[1], free ? "free" : err == 1409 ? "held" : "error");
        }
        string result = null;
        var t = new Thread(() => {
            // The page document: a few levels into the window's tree.
            IAccessible doc = null; var queue = new List<KeyValuePair<IAccessible, int>> { new KeyValuePair<IAccessible, int>(Client(main), 0) };
            for (int at = 0; at < queue.Count && at < 64 && doc == null; ++at) {
                var a = queue[at].Key; if (a == null) continue;
                if (Role(a) == 0xF) { doc = a; break; }
                if (queue[at].Value < 8) foreach (var c in Children(a)) queue.Add(new KeyValuePair<IAccessible, int>(c, queue[at].Value + 1));
            }
            if (doc == null) { result = "{\"error\":\"no page document\"}"; return; }
            bool probed = false;
            if (Kids(doc) == 0) {
                // Chromium's screen-reader probe, then a name read.
                foreach (var h in hosts) { IntPtr r; SendMessageTimeoutW(h, 0x003D, IntPtr.Zero, new IntPtr(1), 0x2, 250, out r); }
                try { doc.get_accName(0); } catch { }
                probed = true;
                for (int k = 0; k < 20 && Kids(doc) == 0; ++k) Thread.Sleep(100);
            }
            var nodes = new StringBuilder(); var stack = new List<Tuple<IAccessible, int, int>> { Tuple.Create(doc, -1, 0) }; int id = 0;
            var watch = Stopwatch.StartNew();
            while (stack.Count > 0 && id < 3000 && watch.ElapsedMilliseconds < 15000) {
                var item = stack[0]; stack.RemoveAt(0);
                var a = item.Item1; int l = 0, tp = 0, w = 0, h = 0; try { a.accLocation(out l, out tp, out w, out h, 0); } catch { }
                string name = "", desc = ""; try { name = a.get_accName(0) ?? ""; } catch { } try { desc = a.get_accDescription(0) ?? ""; } catch { }
                if (nodes.Length > 0) nodes.Append(",");
                nodes.AppendFormat("{{\"id\":{0},\"parent\":{1},\"role\":\"{2}\",\"name\":\"{3}\",\"description\":\"{4}\",\"bounds\":[{5},{6},{7},{8}],\"attributes\":\"{9}\"}}",
                    id, item.Item2 < 0 ? "null" : item.Item2.ToString(), RoleName(Role(a)), Escape(Safe(name)), Escape(Safe(desc)), l, tp, w, h, Escape(SafeAttributes(Ia2(a))));
                int self = id++;
                // Breadth first, in child order, so ids stay in document order per level.
                foreach (var c in Children(a)) stack.Add(Tuple.Create(c, self, item.Item3 + 1));
            }
            result = "{\"schemaVersion\":1,\"source\":\"now_playing_player_tree_probe.ps1 (labels redacted)\",\"probed\":" + (probed ? "true" : "false") +
                ",\"hotkeys\":{" + hot + "},\"nodes\":[" + nodes + "]}";
        });
        t.SetApartmentState(ApartmentState.MTA); t.Start(); t.Join();
        return result;
    }
}
'@
$json = [NowPlayingTreeProbe]::Run($Image, $Class)
[IO.File]::WriteAllText($Out, $json, [Text.UTF8Encoding]::new($false))
Write-Output ("Wrote " + $Out)
