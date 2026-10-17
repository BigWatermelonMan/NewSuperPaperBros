namespace NewSuperPaperBros;

static class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        // Headless self-test / tooling: "--extract <image> <outdir>" writes <outdir>\extract.log and exits 0 on success.
        if (args.Length == 3 && args[0] == "--extract")
        {
            var log = new List<string>();
            try
            {
                using var disc = WiiDisc.Open(args[1]);
                log.Add($"game {disc.GameId} rev {disc.Revision} \"{disc.Title}\" problem=\"{disc.Problem()}\"");
                if (disc.Problem().Length > 0) { Directory.CreateDirectory(args[2]); File.WriteAllLines(Path.Combine(args[2], "extract.log"), log); return 3; }
                int n = disc.Extract(args[2], (done, total, name) => { if (done % 500 == 0) log.Add($"{done}/{total} {name}"); });
                log.Add($"extracted {n} files, holes {disc.Holes}");
                Directory.CreateDirectory(args[2]);
                File.WriteAllLines(Path.Combine(args[2], "extract.log"), log);
                return disc.Holes == 0 ? 0 : 2;
            }
            catch (Exception e)
            {
                log.Add("ERROR " + e);
                Directory.CreateDirectory(args[2]);
                File.WriteAllLines(Path.Combine(args[2], "extract.log"), log);
                return 1;
            }
        }
        ApplicationConfiguration.Initialize();
        // "--screenshot <dir>" draws every page to <dir>\<page>.png and exits (visual check without the desktop).
        int shot = Array.IndexOf(args, "--screenshot");
        Application.Run(new MainForm(selfTest: args.Contains("--selftest"), screenshotDir: shot >= 0 && shot + 1 < args.Length ? args[shot + 1] : null));
        return 0;
    }
}
