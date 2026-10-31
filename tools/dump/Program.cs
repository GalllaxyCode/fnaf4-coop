// Headless reader: loads a Fusion exe with Nebula.Core and writes the event sheets as text.
using System.Collections;
using System.Reflection;
using System.Text;
using Joveler.Compression.ZLib;
using Nebula;
using Nebula.Core.Data.Chunks.FrameChunks.Events;
using Nebula.Core.FileReaders;
using Nebula.Core.Memory;
using Nebula.Core.Utilities;

string exe = Path.GetFullPath(args[0]), outDir = Path.GetFullPath(args[1]);
Directory.CreateDirectory(outDir);
Directory.SetCurrentDirectory(outDir);
ZLibInit.GlobalInit(args.Length > 2 ? args[2] : "/usr/lib/libz.1.dylib");
var p = Parameters.Inst;
p.ignore_images = p.ignore_sounds = p.ignore_music = p.ignore_fonts = p.ignore_shaders = true;

NebulaCore.FilePath = exe;
var r = new EXEFileReader { FilePath = exe };
NebulaCore.CurrentReader = r;
var br = new ByteReader(exe, FileMode.Open);
// LoadGame() extracts icons through kernel32, so its other two steps are called directly.
typeof(EXEFileReader).GetMethod("calculateEntryPoint", BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Public)!.Invoke(r, new object[] { br });
r.Package.PackData.Read(br);
r.Package.Read(br);

var pkg = NebulaCore.PackageData;
var sb = new StringBuilder();
sb.AppendLine($"app={pkg.AppName} build={NebulaCore.Build} fusion={NebulaCore.Fusion} type={NebulaCore.GameType()} frames={pkg.Frames.Count}");
for (int i = 0; i < pkg.Frames.Count; i++) { var fr = pkg.Frames[i]; sb.AppendLine($"frame {i}: {fr.FrameName} events={fr.FrameEvents?.Events.Count}"); }
File.WriteAllText("summary.txt", sb.ToString());

IDictionary Items() => (IDictionary)pkg.FrameItems.GetType().GetField("Items")!.GetValue(pkg.FrameItems)!;
var names = new StringBuilder();
foreach (DictionaryEntry kv in Items())
{
    dynamic o = kv.Value;
    names.AppendLine($"{kv.Key}\t{o.Header.Type}\t{o.Name}");
}
File.WriteAllText("objects.tsv", names.ToString());

string Name(ushort h, short type)
{
    if (type < 0) return "sys" + type;
    var items = Items();
    return items.Contains((int)h) ? ((dynamic)items[(int)h]).Name : "#" + h;
}
string Val(object v, int depth = 0)
{
    if (v == null) return "null";
    if (v is string || v.GetType().IsPrimitive || v.GetType().IsEnum) return v.ToString();
    if (depth > 4) return "...";
    if (v is IEnumerable e) { var l = new List<string>(); foreach (var x in e) l.Add(Val(x, depth + 1)); return "[" + string.Join(", ", l) + "]"; }
    var parts = new List<string>();
    foreach (var f in v.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
    {
        if (f.Name is "ChunkName" or "ChunkID" or "ChunkSize" or "Parent" or "FrameEvents" || f.FieldType.Name.Contains("BitDict") || f.FieldType.Name == "Frame") continue;
        object x; try { x = f.GetValue(v); } catch { continue; }
        if (x is byte[]) continue;
        parts.Add(f.Name + "=" + Val(x, depth + 1));
    }
    return v.GetType().Name.Replace("Parameter", "") + "{" + string.Join(" ", parts) + "}";
}
string Ace(ACEventBase c) => $"{Name(c.ObjectInfo, c.ObjectType)}[t{c.ObjectType} n{c.Num} o{c.ObjectInfo}]" + (c.OtherFlags["Negated"] ? " NOT" : "") + " (" + string.Join(" | ", c.Parameters.Select(x => $"p{x.Code}:{Val(x.Data)}")) + ")";

for (int i = 0; i < pkg.Frames.Count; i++)
{
    var fr = pkg.Frames[i];
    if (fr.FrameEvents == null) continue;
    var w = new StringBuilder();
    int g = 0;
    foreach (var ev in fr.FrameEvents.Events)
    {
        w.AppendLine($"== g{g++} flags={ev.EventFlags.Value:x}");
        foreach (var c in ev.Conditions) w.AppendLine("  IF " + Ace(c));
        foreach (var a in ev.Actions) w.AppendLine("  DO " + Ace(a));
    }
    File.WriteAllText($"events-{i:00}-{string.Concat(fr.FrameName.Split(Path.GetInvalidFileNameChars())).Replace(' ', '_')}.txt", w.ToString());
}
Console.WriteLine(File.ReadAllText("summary.txt"));
