# Native death detachment trace

This probe runs the installed pinned `0.111.0` assembly and resource pack in
Godot 4.5.1 Mono. It executes native Strike or Dagger Throw through an actual
`PlayCardAction`, `ActionQueueSet`, `HookPlayerChoiceContext`, and queued
`GenericHookGameAction`. No gameplay method or scheduler is patched.

```sh
python3.12 tools/oracle/check_native_detachment.py \
  --game-dir '/path/to/Slay the Spire 2' \
  --godot '/path/to/Godot_v4.5.1-stable_mono_linux.x86_64' \
  --output-dir /tmp/native-death-detachment \
  --fixture tests/fixtures/death-detachment-native-v1.json
```

The output directory must not exist. Requires .NET 9, Python 3.11 or later,
and the Mono Godot distribution's bundled NuGet packages. Game files are read
only. The runner retains build/runtime logs, hashes of the assembly, pack,
Godot executable and sources, assembly MVID, and two independent identical
traces per scenario. Every runtime error fails the check, apart from enumerated
native resource-cache allocation diagnostics after successful completion.

## Setup and input seam

Native test setup creates an Ironclad run with seed `ORACLECHECK01`, source card
at permanent deck index 0 and nine Defends at indices 1–9. It starts combat with
two native Shrinker Beetles, then authors HP 6/40 and 40/40 and the declared piles.
Gremlin Horn is obtained before combat; Stratagem 1 is applied before the hit.
Strike starts alone in hand with nine Defends discarded. Dagger Throw retains
Defend 1 in hand and discards Defends 2–9. Both start with an empty draw pile.
Native initialization consumes its initial shuffle and draw normally.

`CardSelectCmd.UseSelector(provider, localOnly: true)` supplies input. The
ordinary global `Selector` remains null. Consequently the native commands still
reserve choice IDs, await `SignalPlayerChoiceBegun`, capture the live candidate
pile, and await `SignalPlayerChoiceEnded`. This differs from the ordinary test
selector, which bypasses signalling and cannot establish detachment timing.
The probe records the actual local provider's ordered candidates and answers its
first candidate. It observes the actual enqueued Gremlin Horn action and checks
that the retained task and its queue action both finish exactly once.

Presentation nodes are absent; native `TestMode` setup and `AutoSlayerCheck`
suppress presentation work and waits. This input seam bypasses native UI
callbacks and `SyncLocalChoice`; no UI, multiplayer synchronization, wall-clock,
or full-game-startup parity is claimed. The output is an explicit scheduling
trace, not the canonical native JSON export (which still rejects detached work).

## Observed order

Strike finishes and enters discard, the dead enemy leaves combat membership,
and its cleanup finishes before the Gremlin Horn/Stratagem choice is presented.
Horn's energy is already gained. After selecting one card, its interrupted draw
adds one more card, with no repeated caller effect.

Dagger Throw demonstrates that an ordinary blocking choice in the still-running
caller has priority over the queued Horn action. The caller draws one card and
presents its discard choice first. Resolving that choice lets the source finish;
only then does the queued Horn action present Stratagem's live draw-pile choice.
Thus FIFO ordering among queued hook actions does not put an older hook action
ahead of the currently executing action's own blocking input.

Source anchors in the pinned reconstruction are `Hook.AfterDeath`,
`HookPlayerChoiceContext.AssignTaskAndWaitForPauseOrCompletion` and
`SignalPlayerChoiceBegun/Ended`, `GenericHookGameAction.ExecuteAction`,
`CreatureCmd.KillWithoutCheckingWinCondition`, `GremlinHorn.AfterDeath`,
`StratagemPower.AfterShuffle`, and `DaggerThrow.OnPlay`.
