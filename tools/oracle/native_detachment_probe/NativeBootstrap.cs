using System;
using System.Linq;
using System.Threading.Tasks;
using MegaCrit.Sts2.Core.Combat;
using MegaCrit.Sts2.Core.Commands;
using MegaCrit.Sts2.Core.Context;
using MegaCrit.Sts2.Core.Entities.Cards;
using MegaCrit.Sts2.Core.Entities.Creatures;
using MegaCrit.Sts2.Core.Entities.Players;
using MegaCrit.Sts2.Core.GameActions;
using MegaCrit.Sts2.Core.GameActions.Multiplayer;
using MegaCrit.Sts2.Core.Models;
using MegaCrit.Sts2.Core.Models.Characters;
using MegaCrit.Sts2.Core.Models.Cards;
using MegaCrit.Sts2.Core.Models.Relics;
using MegaCrit.Sts2.Core.Models.Powers;
using MegaCrit.Sts2.Core.Multiplayer;
using MegaCrit.Sts2.Core.Runs;
using MegaCrit.Sts2.Core.Saves;
using MegaCrit.Sts2.Core.TestSupport;
using MegaCrit.Sts2.Core.Unlocks;

public static class NativeBootstrap {
    public static async Task<object> Run(Func<Task> nextFrame) {
            SetUp();
            var observations=new System.Collections.Generic.List<object>();
            var effect=BeginAttack();
            async System.Threading.Tasks.Task WaitChoice() {
                for(int tick=0;tick<300 && Input.Pending==null;tick++)await nextFrame();
                if(Input.Pending==null)throw new Exception("No input request");
            }
            await WaitChoice();
            observations.Add(Observe(IsDagger?"caller-choice":"horn-choice",effect));
            Input.Choose(0);
            if(IsDagger) {
                await WaitChoice();
                observations.Add(Observe("horn-choice",effect));
                Input.Choose(0);
            }
            for(int tick=0;tick<300 && !Settled();tick++)await nextFrame();
            if(!Settled())throw new Exception("Native queue did not complete");
            await effect;
            if(Action.Exception!=null)throw new InvalidOperationException("Native action failed",Action.Exception);
            if(HookActions.Count!=1 || HookActions.Any(h=>!h.CompletionTask.IsCompleted || h.ChoiceContext?.Task?.IsCompleted!=true))throw new Exception("Native hook group did not complete exactly once");
            observations.Add(Observe("complete",effect));
            SelectorScope.Dispose();
            return new{case_name=IsDagger?"dagger-throw":"strike",observations,assembly_mvid=typeof(CardModel).Assembly.ManifestModule.ModuleVersionId.ToString(),runtime_version=Environment.Version.ToString(),framework=System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription};

    }
    public static bool IsDagger => Environment.GetEnvironmentVariable("STS2_DETACHMENT_CASE")=="dagger-throw";
    private static Player Player=null!;
    private static CombatState Combat=null!;
    private static CardModel[] Cards=Array.Empty<CardModel>();
    private static Creature Victim=null!;
    private static PlayCardAction Action=null!;
    private static readonly System.Collections.Generic.List<GenericHookGameAction> HookActions=new();
    public sealed class InputProvider : ICardSelector {
        public TaskCompletionSource<System.Collections.Generic.IEnumerable<CardModel>>? Pending;
        public CardModel[] Candidates=Array.Empty<CardModel>();
        public int Minimum,Maximum;
        public Task<System.Collections.Generic.IEnumerable<CardModel>> GetSelectedCards(System.Collections.Generic.IEnumerable<CardModel> options,int minimum,int maximum) {
            if(Pending!=null)throw new Exception("Overlapping local input");
            Candidates=options.ToArray(); Minimum=minimum; Maximum=maximum;
            Pending=new(); return Pending.Task;
        }
        public CardRewardSelection GetSelectedCardReward(System.Collections.Generic.IReadOnlyList<CardCreationResult> options,System.Collections.Generic.IReadOnlyList<MegaCrit.Sts2.Core.Entities.CardRewardAlternatives.CardRewardAlternative> alternatives)=>throw new NotSupportedException();
        public void Choose(int index) { var pending=Pending!; var selected=Candidates[index]; Pending=null; pending.SetResult(new[]{selected}); }
    }
    public static InputProvider Input=new();
    private static IDisposable SelectorScope=null!;
    public static void SetUp() {
        TestMode.IsOn=true;
        MegaCrit.Sts2.Core.Modding.ModManager.Initialize(new MegaCrit.Sts2.Core.Modding.ModManagerFileIo(),null,null).GetAwaiter().GetResult();
        MegaCrit.Sts2.Core.Modding.AssemblyInfo.Init();
        foreach(var type in AbstractModelSubtypes.All)ModelDb.Inject(type);
        SaveManager.Instance.InitProfileId(0); SaveManager.Instance.InitProgressData();
        Player=Player.CreateForNewRun<Ironclad>(UnlockState.all,1);
        var state=RunState.CreateForTest(players:new[]{Player},seed:"ORACLECHECK01");
        var service=new NetSingleplayerGameService();
        RunManager.Instance.SetUpTest(state,service); LocalContext.NetId=service.NetId;
        Player.Deck.Clear(silent:true);
        var source=IsDagger?(CardModel)state.CreateCard<DaggerThrow>(Player):state.CreateCard<StrikeIronclad>(Player);
        CardPileCmd.Add(source,Player.Deck,skipVisuals:true).GetAwaiter().GetResult();
        for(int i=1;i<10;i++)CardPileCmd.Add(state.CreateCard<DefendIronclad>(Player),Player.Deck,skipVisuals:true).GetAwaiter().GetResult();
        RelicCmd.Obtain<GremlinHorn>(Player).GetAwaiter().GetResult();
        var encounter=ModelDb.Encounter<MegaCrit.Sts2.Core.Models.Encounters.ShrinkerBeetleWeak>().ToMutable();
        encounter.GenerateMonstersWithSlots(state);
        Combat=new CombatState(encounter:encounter,runState:state); Combat.AddPlayer(Player);
        var (monster,slot)=encounter.MonstersWithSlots.First();
        Combat.AddCreature(Combat.CreateCreature(monster,CombatSide.Enemy,slot));
        Combat.AddCreature(Combat.CreateCreature(ModelDb.GetById<MonsterModel>(monster.Id).ToMutable(),CombatSide.Enemy,null));
        CombatManager.Instance.SetUpCombat(Combat); CombatManager.Instance.AfterCombatRoomLoaded();
        if(!CombatManager.Instance.IsInProgress)throw new Exception("Native combat not started");
        Cards=Player.PlayerCombatState!.AllPiles.SelectMany(p=>p.Cards).OrderBy(c=>Array.IndexOf(Player.Deck.Cards.ToArray(),c.DeckVersion)).ToArray();
        Cards=Cards.OrderBy(c=>c is DefendIronclad?1:0).ToArray();
        foreach(var card in Cards)CardPileCmd.Add(card,PileType.Discard,skipVisuals:true).GetAwaiter().GetResult();
        CardPileCmd.Add(Cards[0],PileType.Hand,skipVisuals:true).GetAwaiter().GetResult();
        if(IsDagger)CardPileCmd.Add(Cards[1],PileType.Hand,skipVisuals:true).GetAwaiter().GetResult();
        Victim=Combat.Enemies[0]; Victim.SetMaxHpInternal(40); Victim.SetCurrentHpInternal(6);
        Combat.Enemies[1].SetMaxHpInternal(40); Combat.Enemies[1].SetCurrentHpInternal(40);
        PowerCmd.Apply<StratagemPower>(new BlockingPlayerChoiceContext(),Player.Creature,1,Player.Creature,null,silent:true).GetAwaiter().GetResult();
        SelectorScope=CardSelectCmd.UseSelector(Input,localOnly:true);
    }
    public static Task BeginAttack() {
        MegaCrit.Sts2.Core.Helpers.NonInteractiveMode.AutoSlayerCheck=()=>true;
        RunManager.Instance.ActionQueueSet.ActionEnqueued += action=>{if(action is GenericHookGameAction hook)HookActions.Add(hook);};
        Action=new PlayCardAction(Cards[0],Victim);
        RunManager.Instance.ActionQueueSet.EnqueueWithoutSynchronizing(Action);
        return Action.CompletionTask;
    }
    public static bool Settled()=>!RunManager.Instance.ActionExecutor.IsRunning && RunManager.Instance.ActionExecutor.CurrentlyRunningAction==null && Input.Pending==null;
    private static int Index(CardModel card)=>Array.IndexOf(Cards,card);
    public static object Observe(string stage,Task task) => new {
        stage, action_completed=task.IsCompleted, action_exception=Action.Exception?.ToString(),
        energy=Player.PlayerCombatState!.Energy,
        native_hook_actions=HookActions.Select(hook=>new{hook_id=hook.HookId,source=hook.ChoiceContext?.Source?.Id.ToString(),task_completed=hook.ChoiceContext?.Task?.IsCompleted,action_completed=hook.CompletionTask.IsCompleted}).ToArray(),
        victim_member=Combat.Enemies.Contains(Victim),victim_hp=Victim.CurrentHp,victim_powers=Victim.Powers.Count,
        enemies=Combat.Enemies.Select(e=>e.CurrentHp).ToArray(),
        piles=Player.PlayerCombatState.AllPiles.Select(p=>new{zone=p.Type.ToString(),cards=p.Cards.Select(Index).ToArray()}).ToArray(),
        current_action=RunManager.Instance.ActionExecutor.CurrentlyRunningAction?.GetType().Name,
        hook_task_completed=(RunManager.Instance.ActionExecutor.CurrentlyRunningAction as GenericHookGameAction)?.ChoiceContext?.Task?.IsCompleted,
        candidates=Input.Candidates.Select(Index).ToArray(),minimum=Input.Minimum,maximum=Input.Maximum,
        draw_order=Player.PlayerCombatState.DrawPile.Cards.Select(Index).ToArray(),
        cards=Cards.Select((c,i)=>new{index=i,id=c.Id.ToString(),deck_index=Array.IndexOf(Player.Deck.Cards.ToArray(),c.DeckVersion)}).ToArray()
    };
}
