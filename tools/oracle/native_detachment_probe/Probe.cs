using Godot;
using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
public partial class Probe : Node {
    public override async void _Ready() {
        try {
            var dir=System.Environment.GetEnvironmentVariable("STS2_CHOICE_GAME_DLL_DIR")!;
            AssemblyLoadContext.GetLoadContext(typeof(Probe).Assembly)!.Resolving += (ctx,name) => {
                var p=Path.Combine(dir,name.Name+".dll"); return File.Exists(p) ? ctx.LoadFromAssemblyPath(p) : null;
            };
            var a=AssemblyLoadContext.GetLoadContext(typeof(Probe).Assembly)!.LoadFromAssemblyPath(Path.Combine(dir,"sts2.dll"));
            GD.Print("PROBE context ",AssemblyLoadContext.GetLoadContext(a)," ",AssemblyLoadContext.GetLoadContext(typeof(Node).Assembly));
            Godot.Bridge.ScriptManagerBridge.LookupScriptsInAssembly(a);
            GD.Print("PROBE assembly ",a.FullName," Godot ",typeof(Node).Assembly.FullName);
            GD.Print("PROBE pack ",ProjectSettings.LoadResourcePack(System.Environment.GetEnvironmentVariable("STS2_CHOICE_GAME_PACK")!,false));
            var test=a.GetType("MegaCrit.Sts2.Core.TestSupport.TestMode",true)!;
            test.GetProperty("IsOn")!.SetValue(null,true);
            var save=a.GetType("MegaCrit.Sts2.Core.Saves.SaveManager",true)!.GetProperty("Instance")!.GetValue(null)!;
            save.GetType().GetMethod("InitSettingsDataForTest")!.Invoke(save,null);
            save.GetType().GetMethod("InitPrefsDataForTest")!.Invoke(save,null);
            save.GetType().GetProperty("SettingsSave")!.GetValue(save)!.GetType().GetProperty("Language")!.SetValue(save.GetType().GetProperty("SettingsSave")!.GetValue(save),"eng");
            a.GetType("MegaCrit.Sts2.Core.Localization.LocManager",true)!.GetMethod("Initialize")!.Invoke(null,null);
            var bootstrap=typeof(Probe).Assembly.GetType("NativeBootstrap",true)!;
            var task=(System.Threading.Tasks.Task<object>)bootstrap.GetMethod("Run")!.Invoke(null,new object[]{(Func<System.Threading.Tasks.Task>)(async()=>{await ToSignal(GetTree(),SceneTree.SignalName.ProcessFrame);})})!;
            var result=await task;
            GD.Print("DETACHMENT_RESULT ",System.Text.Json.JsonSerializer.Serialize(result));
            GD.Print("PROBE CLEANUP COMPLETE");
        } catch(Exception ex){GD.PrintErr("PROBE ERROR ",ex);GetTree().Quit(1);return;}
        GetTree().Quit();
    }
}
