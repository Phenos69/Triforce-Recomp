using System.Text;
using Triforce.Logic;
using static Triforce.LogicTests.T;

namespace Triforce.LogicTests;

static partial class Tests
{
    public static IEnumerable<(string Name, Func<Task> Run)> All()
    {
        (string, Func<Task>) Sync(string n, Action a) => (n, () => { a(); return Task.CompletedTask; });
        yield return Sync("settings: registry.txt round trip", SettingsTestRoot);
        yield return Sync("settings: builder.ini round trip (Linux store)", SettingsIni);
        yield return Sync("startup: argument parsing", StartArgsParsing);
        yield return Sync("startup: lock name algorithm", LockName);
        yield return Sync("startup: installed run, old exe removed, second start defers", StartupInstalled);
        yield return Sync("startup: not installed shows Install; uninstall refused while open", StartupNotInstalled);
        yield return Sync("install + uninstall in a test root", InstallUninstall);
        yield return ("release: check, parse, notes against a fake server", ReleaseCheck);
        yield return ("release: download with size check and resume", DownloadResume);
        yield return ("release: self-swap, new launcher up", SelfSwapUp);
        yield return ("release: self-swap, new launcher fails, rolled back", SelfSwapFails);
        yield return Sync("release: decide what the start does", Decide);
        yield return Sync("tools: unpack the whole zip, refuse unsafe names", ToolsUnpack);
        yield return Sync("tools: base list and parts", ToolsParts);
        yield return ("tools: update from the fake server with progress", ToolsUpdate);
        yield return Sync("build: @@ protocol from a recorded transcript", Protocol);
        yield return Sync("build: estimates from history and without", Estimates);
        yield return ("build: runner with a fake make_game (done, args, log, times)", RunnerDone);
        yield return ("build: runner stop kills the tree", RunnerStop);
        yield return ("build: runner failure and test-root refusal", RunnerFail);
        yield return Sync("game: the built test game folder", GameFolder);
        yield return Sync("intake: folders, .nup header, packed romfs, space", Intake);
        yield return ("intake: make_game --check (fake)", IntakeCheck);
        yield return Sync("saves: slots of lab-game-merge98 (read only)", SavesListing);
        yield return ("saves: find, import, export with backups", SavesCopy);
        yield return Sync("mods: list, order, on/off, remove, rebuild mark", ModsFolder);
        yield return Sync("mods: add archives and their safety checks", ModsArchives);
        yield return ("mods: GameBanana install into the mods folder (fake server)", GameBananaInstall);
        yield return ("mods: GameBanana live list (read only)", GameBananaLive);
        yield return Sync("mods: GameBanana page parsing and plain text", GameBananaParse);
        yield return Sync("linux: release keys, modes, parts list", LinuxLogic);
        yield return Sync("steam: add, set, remove in a fake Steam folder", SteamShortcut);
        yield return Sync("report: names out", ReportNames);
        yield return Sync("languages", LanguagesTest);
    }

    // ---- settings ----

    static void SettingsTestRoot()
    {
        var root = NewRoot("settings");
        var ctx = Ctx(root);
        ctx.Settings.GameFolder = @"D:\Games\Triforce";
        ctx.Settings.Language = "de";
        ctx.Settings.Dlss = true;
        ctx.Settings.Set("Language", "fr");
        ctx.Platform.Settings.SetUninstall("InstallLocation", root);
        ctx.Platform.Settings.SetUninstallNumber("NoModify", 1);
        var again = Ctx(root);
        Equal(@"D:\Games\Triforce", again.Settings.GameFolder, "GameFolder read back");
        Equal("fr", again.Settings.Language, "the last value for a name wins");
        Check(again.Settings.Dlss, "Dlss read back");
        Equal(40, again.Settings.MusicVolume, "music volume default");
        again.Settings.MusicVolume = 250;
        Equal(100, Ctx(root).Settings.MusicVolume, "volume clamped");
        var lines = File.ReadAllLines(Path.Combine(root, "registry.txt"));
        Check(lines.Contains(@"Software\Triforce\GameFolder=D:\Games\Triforce"), "the old Setup's line format: " + string.Join(" / ", lines));
        Check(lines.Contains(@"Software\Microsoft\Windows\CurrentVersion\Uninstall\Triforce\NoModify=1"), "uninstall entry lines");
        Equal(1, lines.Count(l => l.StartsWith(@"Software\Triforce\Language=")), "one line per name after a write");
        again.Platform.Settings.RemoveAll();
        Equal(null, Ctx(root).Settings.Language, "settings removed");
        Equal(root, Ctx(root).Platform.Settings.GetUninstall("InstallLocation"), "uninstall entry kept until removed");
        var snap = Ctx(root);
        snap.NoWrites = true;
        snap.Settings.Language = "ja";
        Equal(null, Ctx(root).Settings.Language, "NoWrites writes nothing");
    }

    static void SettingsIni()
    {
        var root = NewRoot("ini");
        var file = Path.Combine(root, "triforce", "builder.ini");
        Directory.CreateDirectory(Path.GetDirectoryName(file)!);
        File.WriteAllText(file, "kind=nup\nnup=/home/a/oot.nup\nout=/home/a/Games/Old\nlanguage=en-US\nprepare=1\nout=/home/a/Games/Triforce\n");
        var s = new IniSettingsStore(file);
        Equal("/home/a/Games/Triforce", s.Get(SettingNames.GameFolder), "GameFolder is the last out= line (hookshot-launch)");
        s.Set(SettingNames.Language, "ja");
        s.Set(SettingNames.Dlss, "1");
        s.Set(SettingNames.SetupSkip, "v0.5.2");
        var text = File.ReadAllText(file);
        Check(text.Contains("kind=nup\n") && text.Contains("prepare=1\n") && text.Contains("nup=/home/a/oot.nup\n"), "the Tk builder's keys kept: " + text);
        Check(text.Contains("language=ja\n") && text.Contains("dlss=1\n") && text.Contains("SetupSkip=v0.5.2\n"), "written under the shared names: " + text);
        Equal(1, text.Split('\n').Count(l => l.StartsWith("out=")), "one out= line after a rewrite");
        Throws<ArgumentException>(() => s.Set("x", "a\nb"), "a line break is refused");
    }

    // ---- startup ----

    static void StartArgsParsing()
    {
        var a = StartArgs.Parse(new[] { "--relaunched", "--merge" });
        Check(a.Relaunched && a.Merge && a.UpdateFolder == null, "--relaunched --merge");
        a = StartArgs.Parse(new[] { "--update", @"C:\Games\Triforce", "--relaunched" });
        Check(a.UpdateFolder == @"C:\Games\Triforce" && a.Merge && a.Relaunched, "--update sets merge");
        a = StartArgs.Parse(new[] { "--update", @"C:\Games\Triforce", "--tag", "v0.5.2" });
        Equal("v0.5.2", a.Tag, "--tag from the game");
        // (a folder ending in a backslash, quoted on a command line: \" escapes the quote)
        a = StartArgs.Parse(new[] { "--update", "C:\\Games\\Triforce\" --tag v0.5.2" });
        Equal(@"C:\Games\Triforce\", a.UpdateFolder, "the folder repaired");
        Equal("v0.5.2", a.Tag, "and the tag after it");
        a = StartArgs.Parse(new[] { "--uninstall", "--quiet", "--something-new", "x" });
        Check(a.Uninstall && a.Quiet && a.Unknown.Contains("--something-new"), "unknown arguments kept aside, not fatal");
        a = StartArgs.Parse(new[] { "--launch", "/home/a/Games/Triforce" });
        Equal("/home/a/Games/Triforce", a.LaunchFolder, "Linux --launch");
        a = StartArgs.Parse(new[] { "--update", "/g", "--tag", "v1", "--fetched" });
        Check(a.Fetched && a.UpdateFolder == "/g" && a.Tag == "v1", "Linux --update --tag --fetched");
        a = StartArgs.Parse(new[] { "--install-to", @"D:\T", "--no-desktop" });
        Check(a.InstallTo == @"D:\T" && a.NoDesktop && !a.NoStartMenu, "--install-to");
        Equal("--relaunched|--merge", string.Join("|", new StartArgs { Merge = true }.PassOn(true)), "self-update passes --relaunched --merge");
        Equal("--update|C:\\G|--relaunched", string.Join("|", new StartArgs { Merge = true, UpdateFolder = @"C:\G" }.PassOn(true)), "with the game's folder");
        Equal("--merge", string.Join("|", new StartArgs { Merge = true }.PassOn(false)), "install relaunch");
    }

    static void LockName()
    {
        var a = @"C:\Users\you\AppData\Local\Programs\Triforce";
        var name = InstanceLock.Name(a, null);
        Equal(@"Local\TriforceSetup-8f6ec578ae853054-C--USERS-YOU-APPDATA-LOCAL-PROGRAMS-TRIFORCE", name, "FNV-1a 64 + tail, as the old Setup (PowerShell reference)");
        Equal(name, InstanceLock.Name(a.ToLowerInvariant() + @"\", null), "case and a trailing slash");
        Equal(name, InstanceLock.Name(@"C:\Users\you\AppData\Local\Programs\Other\..\Triforce", null), "another way to the same folder");
        Check(name != InstanceLock.Name(a, @"E:\scratch\root"), "a test root names another lock");
        Equal(name + "-up", InstanceLock.UpName(a, null), "the handover sign");
        var tail = InstanceLock.Name(@"C:\" + new string('x', 400), null);
        Check(tail.Length < 200 && tail.EndsWith(new string('X', 60)), "the tail is the last 60 characters");
    }

    static void StartupInstalled()
    {
        var root = NewRoot("startup");
        var dir = Path.Combine(root, "Programs", "Triforce");
        Directory.CreateDirectory(dir);
        var exe = Path.Combine(dir, BuildInfo.SetupAsset);
        File.WriteAllText(exe, "installed");
        File.WriteAllText(Path.Combine(dir, "TriforceSetup.old.exe"), "replaced one");
        var ctx = Ctx(root, exe);
        ctx.Platform.Settings.SetUninstall("InstallLocation", dir);
        Check(ctx.RunningInstalled, "running installed");
        var r = Startup.Run(ctx, new[] { "--relaunched", "--merge" });
        try
        {
            Equal(StartScreen.Check, r.Screen, "an installed run checks for updates");
            Check(r.Args.Merge && r.SignalUpWhenShown, "merge carried, up sign asked");
            Check(!File.Exists(Path.Combine(dir, "TriforceSetup.old.exe")), "TriforceSetup.old.exe removed");
            Check(r.Lock is { Held: true }, "the lock held");
            var second = Startup.Run(Ctx(root, exe), Array.Empty<string>());
            Equal(StartScreen.Exit, second.Screen, "a second start ends");
            Equal(0, second.ExitCode, "after asking the first to the front");
            var un = Startup.Run(Ctx(root, exe), new[] { "--uninstall", "--quiet" });
            Check(un.Screen == StartScreen.Exit && un.ExitCode == 1 && un.Message == null, "uninstall refused while open, quietly");
        }
        finally { r.Lock?.Dispose(); }
        var after = Startup.Run(Ctx(root, exe), Array.Empty<string>());
        try { Equal(StartScreen.Check, after.Screen, "the lock is free again after Release"); }
        finally { after.Lock?.Dispose(); }
    }

    static void StartupNotInstalled()
    {
        var root = NewRoot("notinstalled");
        var ctx = Ctx(root, Path.Combine(root, "Downloads", BuildInfo.SetupAsset));
        var r = Startup.Run(ctx, Array.Empty<string>());
        try { Equal(StartScreen.Install, r.Screen, "a download that is not installed shows Install"); }
        finally { r.Lock?.Dispose(); }
        var u = Startup.Run(ctx, new[] { "--uninstall" });
        try { Equal(StartScreen.Uninstall, u.Screen, "uninstall goes on when nothing is open"); }
        finally { u.Lock?.Dispose(); }
        // (installed elsewhere, started from a download: the download becomes the installed launcher; here that copy cannot start,
        // so this one carries on)
        var dir = Path.Combine(root, "Programs", "Triforce");
        var self = Path.Combine(root, "Downloads", BuildInfo.SetupAsset);
        Directory.CreateDirectory(dir);
        Directory.CreateDirectory(Path.GetDirectoryName(self)!);
        File.WriteAllText(self, "downloaded copy, no program");
        File.WriteAllText(Path.Combine(dir, BuildInfo.SetupAsset), "installed copy");
        var c2 = Ctx(root, self);
        c2.Platform.Settings.SetUninstall("InstallLocation", dir);
        var r2 = Startup.Run(c2, new[] { "--merge" });
        try
        {
            Equal("downloaded copy, no program", File.ReadAllText(Path.Combine(dir, BuildInfo.SetupAsset)), "the download was copied over the installed copy");
            Equal(StartScreen.Check, r2.Screen, "and, the installed copy not starting, this one carries on");
        }
        finally { r2.Lock?.Dispose(); }
    }

    static void InstallUninstall()
    {
        var root = NewRoot("install");
        var self = Path.Combine(root, "Downloads", BuildInfo.SetupAsset);
        Directory.CreateDirectory(Path.GetDirectoryName(self)!);
        File.WriteAllText(self, "launcher bytes");
        var ctx = Ctx(root, self);
        var inst = new Installer(ctx);
        Equal(Path.Combine(root, "Programs", "Triforce"), inst.DefaultDir, "default install folder under the test root's LocalAppData");
        var exe = inst.Run(inst.DefaultDir, desktop: true, startMenu: true);
        Equal("launcher bytes", File.ReadAllText(exe), "copied");
        Equal(inst.DefaultDir, ctx.InstalledDir, "registered (InstallLocation)");
        Equal("\"" + exe + "\" --uninstall --quiet", ctx.Platform.Settings.GetUninstall("QuietUninstallString"), "quiet uninstall string");
        Check(File.Exists(Path.Combine(root, "Desktop", "Triforce.lnk")), "desktop shortcut in the test root");
        Check(File.Exists(Path.Combine(root, "Start Menu", "Triforce", "Triforce.lnk")), "start menu shortcut in the test root");
        Throws<InvalidOperationException>(() => inst.Run(Path.Combine(Path.GetTempPath(), "elsewhere-" + Guid.NewGuid().ToString("N")), false, false), "outside the test root refused");
        Directory.CreateDirectory(Path.Combine(root, "build", "objcache"));
        ctx.Settings.GameFolder = Path.Combine(root, "Games", "Triforce");
        var plan = new Uninstaller(ctx).Describe();
        Check(plan.Keeps.Any(k => k.Contains("saves")) && plan.Removes.Count >= 3, "the plan says what goes and stays");
        new Uninstaller(ctx).Run();
        Check(!File.Exists(Path.Combine(root, "Desktop", "Triforce.lnk")) && !Directory.Exists(Path.Combine(root, "Start Menu", "Triforce")), "shortcuts gone");
        Check(!Directory.Exists(Path.Combine(root, "build")), "work folder gone");
        Equal(null, Ctx(root, self).Platform.Settings.GetUninstall("InstallLocation"), "uninstall entry gone");
        Equal(null, Ctx(root, self).Settings.GameFolder, "settings gone");
    }

    // ---- report, languages ----

    static void ReportNames()
    {
        var text = @"C:\Users\Ann\AppData\x and c:/users/ann//Games and C:\Users\Anna\y and C:\Users\ANN~1\Temp";
        var o = Report.NoName(text, @"C:\Users\Ann", @"C:\Users\ANN~1");
        Equal(@"%USERPROFILE%\AppData\x and %USERPROFILE%//Games and C:\Users\Anna\y and %USERPROFILE%\Temp", o, "the account's folder read as %USERPROFILE%");
        Equal("b\nc", Report.Tail("a\nb\nc\n\n", 2).Replace("\r", ""), "tail");
    }

    static void LanguagesTest()
    {
        Equal(16, Languages.All.Count, "16 codes");
        Equal("zh-Hant", Languages.Default("zh-TW").Code, "zh-TW");
        Equal("zh-Hans", Languages.Default("zh-CN").Code, "zh-CN");
        Equal("es-419", Languages.Default("es-MX").Code, "es-MX");
        Equal("es", Languages.Default("es-ES").Code, "es-ES");
        Equal("pt-BR", Languages.Default("pt-PT").Code, "pt-PT");
        Equal("de", Languages.Default("de-AT").Code, "de-AT");
        Equal("en-US", Languages.Default("xx-YY").Code, "unknown");
        Equal("fr-CA", Languages.Default("fr-CA").Code, "exact");
    }
}
