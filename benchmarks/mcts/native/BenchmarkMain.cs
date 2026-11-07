using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using System.Text.Json;
using System.Diagnostics;
using Sts2Headless;
using HarmonyLib;
using MegaCrit.Sts2.Core.Commands;
using MegaCrit.Sts2.Core.Combat;

// Alternate entry point compiled alongside unchanged upstream C# sources.
// Only input/output configuration is JSON; search uses dictionaries and callbacks.
class BenchmarkMain {
    [StructLayout(LayoutKind.Sequential)] struct Action { public int Card, Target, EndTurn; }
    [StructLayout(LayoutKind.Sequential)] struct Result {
        public Action Action;
        public ulong Iterations, Transitions, Nodes, Visits, Terminals, Cutoffs;
        public double Mean;
    }
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int IntCallback();
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int ActionCallback(int index, out Action action);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate int ApplyCallback(Action action);
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)] delegate double EvaluateCallback();
    [StructLayout(LayoutKind.Sequential)] struct Callbacks {
        public IntCallback Restore, Count;
        public ActionCallback Action;
        public ApplyCallback Apply;
        public IntCallback Terminal;
        public EvaluateCallback Evaluate;
    }
    [DllImport("mcts_search_bridge", CallingConvention=CallingConvention.Cdecl)]
    static extern int mcts_search(Callbacks callbacks, ulong iterations, ulong horizon, ulong seed, out Result result);

    static void Main(string[] args) {
        try {
            var lib = Environment.GetEnvironmentVariable("STS2_LIB")!;
            AssemblyLoadContext.Default.Resolving += (_, name) => {
                var path = Path.Combine(lib, name.Name + ".dll");
                return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
            };
            Run(args);
        } catch (Exception error) { Console.Error.WriteLine(error); Environment.ExitCode = 1; }
    }
    static Dictionary<string, object> FormatAction(Action action) {
        var value = new Dictionary<string, object> {{"kind", action.EndTurn != 0 ? "end_turn" : "play_card"}};
        if (action.EndTurn == 0) {
            value["card_index"] = action.Card;
            if (action.Target >= 0) value["target_index"] = action.Target;
        }
        return value;
    }
    static readonly List<IDisposable> SelectorScopes = new();
    public static void CaptureSelector(IDisposable __result) => SelectorScopes.Add(__result);
    static void Run(string[] args) {
        // Stock StartRun discards UseSelector's IDisposable because its normal
        // lifetime is one process. Capture it solely for benchmark teardown.
        var selector = typeof(CardSelectCmd).GetMethods().Single(m => m.Name == "UseSelector");
        new Harmony("mcts.benchmark.selector-lifetime").Patch(selector,
            postfix: new HarmonyMethod(typeof(BenchmarkMain).GetMethod(nameof(CaptureSelector))!));
        var config = JsonDocument.Parse(File.ReadAllText(args[0])).RootElement;
        var host = new Host(config.GetProperty("scenario"));
        var callbacks = host.Callbacks(); // Retained until the final native call returns.
        ulong Read(string name, ulong fallback) => config.TryGetProperty(name, out var v) ? v.GetUInt64() : fallback;
        var iterations = Read("iterations", 100); var horizon = Read("horizon", 64); var seed = Read("seed", 1);
        var warmups = Read("warmups", 1); var samples = Read("samples", 3);
        if (iterations == 0 || iterations > 10000000 || horizon == 0 || horizon > 100000 || samples == 0 || samples > 1000 || warmups > 1000)
            throw new Exception("Invalid search budgets");
        var rows = new List<object>();
        try {
            for (ulong i = 0; i < warmups + samples; ++i) {
                var stdout = Console.Out; var stderr = Console.Error;
                Console.SetOut(TextWriter.Null); Console.SetError(TextWriter.Null);
                var watch = Stopwatch.StartNew();
                Result result;
                try {
                    if (mcts_search(callbacks, iterations, horizon, seed, out result) == 0)
                        throw new Exception(host.Error ?? "Native search failed");
                } finally {
                    watch.Stop(); Console.SetOut(stdout); Console.SetError(stderr);
                }
                Console.Error.WriteLine($"Completed {(i < warmups ? "warmup" : "sample")} {i + 1}: {watch.Elapsed.TotalSeconds:F3}s");
                if (i >= warmups) rows.Add(new {
                    seconds = watch.Elapsed.TotalSeconds,
                    action = FormatAction(result.Action),
                    iterations = result.Iterations, transitions = result.Transitions, nodes = result.Nodes,
                    best_visits = result.Visits, best_mean = result.Mean,
                    terminal_rollouts = result.Terminals, cutoffs = result.Cutoffs
                });
            }
            Console.WriteLine(JsonSerializer.Serialize(new { profile = "mcts-native-in-process-replay-v1", samples = rows,
                warmups, root_checks = host.RootChecks, startup_included = false, json_ipc_included = false }));
        } finally { host.Close(); GC.KeepAlive(callbacks); }
    }
    sealed class Host {
        readonly JsonElement _scenario;
        RunSimulator? _sim;
        Dictionary<string, object?> _reply = new();
        List<Action> _actions = new();
        string? _root;
        public string? Error;
        public ulong RootChecks;
        public Host(JsonElement scenario) { _scenario = scenario; }
        int Guard(System.Action operation) { try { operation(); return 1; } catch (Exception e) { Error = e.ToString(); return 0; } }
        static Dictionary<string, object?> Dict(object? value) => (Dictionary<string, object?>)value!;
        static int Number(object? value) => Convert.ToInt32(value);
        static List<Dictionary<string, object?>> List(object? value) => ((IEnumerable<Dictionary<string, object?>>)value!).ToList();
        void Set(Dictionary<string, object?> reply) {
            if ((string?)reply["type"] == "error") throw new Exception(JsonSerializer.Serialize(reply));
            _reply = reply;
        }
        Dictionary<string, object?> Player => Dict(_reply["player"]);
        bool Terminal => Number(Player["hp"]) <= 0 || (string?)_reply["decision"] != "combat_play";
        public void Close() {
            if (_sim == null) return;
            for (int i = SelectorScopes.Count - 1; i >= 0; --i) SelectorScopes[i].Dispose();
            SelectorScopes.Clear();
            // Detach only this simulator's anonymous event handlers. The stock
            // process normally exits instead of reusing these singleton events.
            foreach (var name in new[] { "TurnStarted", "CombatEnded" }) {
                var type = typeof(CombatManager);
                var field = type.GetField(name, BindingFlags.Instance | BindingFlags.NonPublic)
                    ?? throw new Exception("Unsupported event layout: " + name);
                var callbacks = (field.GetValue(CombatManager.Instance) as Delegate)?.GetInvocationList()
                    ?? Array.Empty<Delegate>();
                foreach (var callback in callbacks.Where(c => ReferenceEquals(c.Target, _sim)))
                    type.GetEvent(name)!.RemoveEventHandler(CombatManager.Instance, callback);
            }
            _sim.CleanUp();
            if (CardSelectCmd.Selector != null || CardSelectCmd.LocalSelector != null)
                throw new Exception("Native teardown retained a selector");
            _sim = null;
        }
        void Restore() {
            Close();
            _sim = new RunSimulator();
            var setup = _scenario.GetProperty("setup");
            Set(_sim.StartRun(setup.GetProperty("character").GetString()!.Replace("CHARACTER.", ""),
                setup.GetProperty("ascension").GetInt32(), setup.GetProperty("seed").GetString(), "en"));
            foreach (var operation in _scenario.GetProperty("operations").EnumerateArray()) {
                if (operation.GetProperty("cmd").GetString() != "action") throw new Exception("Only action prefixes supported");
                var action = operation.GetProperty("action");
                var args = action.EnumerateObject().Where(p => p.Name != "kind")
                    .ToDictionary(p => p.Name, p => (object?)p.Value.GetInt32());
                Set(_sim.ExecuteAction(action.GetProperty("kind").GetString()!, args));
            }
            if (Terminal) throw new Exception("Root is not combat");
            var enemies = List(_reply["enemies"]);
            if (enemies.Count != 1) throw new Exception("One enemy required");
            // Compare deterministic public root values, independent of wall-clock metadata.
            var signature = string.Join("|", Number(Player["hp"]), Number(Player["max_hp"]),
                Number(_reply["energy"]), Number(enemies[0]["hp"]), Number(_reply["draw_pile_count"]),
                string.Join(",", List(_reply["hand"]).Select(c => $"{c["id"]}:{c["cost"]}:{c["can_play"]}")));
            _root ??= signature;
            if (_root != signature) throw new Exception("Reconstructed root drifted");
            ++RootChecks;
        }
        void Enumerate() {
            _actions.Clear();
            if (Terminal) return;
            if (List(_reply["enemies"]).Count != 1) throw new Exception("One enemy required");
            foreach (var card in List(_reply["hand"])) {
                if (!(bool)card["can_play"]!) continue;
                var target = (string)card["target_type"]!;
                if (target != "AnyEnemy" && target != "Self" && target != "None") throw new Exception("Unsupported target");
                _actions.Add(new Action { Card = Number(card["index"]), Target = target == "AnyEnemy" ? 0 : -1 });
            }
            _actions.Add(new Action { EndTurn = 1 });
        }
        void Apply(Action action) {
            var args = new Dictionary<string, object?> {{"card_index", action.Card}};
            if (action.Target >= 0) args["target_index"] = action.Target;
            Set(_sim!.ExecuteAction(action.EndTurn != 0 ? "end_turn" : "play_card", args));
        }
        public Callbacks Callbacks() => new() {
            Restore = () => Guard(Restore),
            Count = () => Guard(Enumerate) == 0 ? -1 : _actions.Count,
            Action = (int index, out Action action) => { action = default; try { action = _actions[index]; return 1; } catch(Exception e) { Error = e.ToString(); return 0; } },
            Apply = action => Guard(() => Apply(action)),
            Terminal = () => { try { return Terminal ? 1 : 0; } catch(Exception e) { Error = e.ToString(); return -1; } },
            Evaluate = () => { try { var hp = Number(Player["hp"]); return hp <= 0 ? 0 : (Terminal ? 0.5 : 0) + 0.5 * hp / Number(Player["max_hp"]); } catch(Exception e) { Error = e.ToString(); return double.NaN; } }
        };
    }
}
