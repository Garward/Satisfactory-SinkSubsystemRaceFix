# SinkSubsystemRaceFix — Plan

## Problem

Joining your modded Satisfactory dedicated server crashes the client. We have observed
**two distinct crashes** in this family — both caused by client-side mod/game code
touching a vanilla world subsystem during the `BeginPlay` cascade before that subsystem
has replicated from the server.

### Crash A — Resource Sink subsystem race

```
Assertion failed: mResourceSinkSubsystem
File: FGBuildableResourceSink.cpp  Line: 25

AFGBuildableResourceSink::BeginPlay()
AActor::DispatchBeginPlay()
AWorldSettings::NotifyBeginPlay()
AFGWorldSettings::NotifyBeginPlay()
AGameStateBase::OnRep_ReplicatedHasBegunPlay()
... (replication path)
```

Resource Sink buildables (`AFGBuildableResourceSink`) grab their world-singleton
subsystem (`AFGResourceSinkSubsystem`) at line 24 and `check()` it at line 25. If the
subsystem hasn't replicated yet the assert kills the process.

### Crash B — Schematic manager race

```
Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x00000000000002b8

UFGSchematic::CanGiveAccessToSchematic() [FGSchematic.cpp:408]
... (CoreUObject blueprint VM frames)
SML
... (Engine replication path)
AGameStateBase::OnRep_ReplicatedHasBegunPlay()
```

`UFGSchematic::CanGiveAccessToSchematic` is a static helper called during the same
cascade — typically by mod blueprints walking schematic CDOs (e.g. `AdvancedRecipes`
iterating `WasteShielding` schematics). It looks up `AFGSchematicManager` from the
world; on the client that manager hasn't replicated yet and the dereference faults.

### Common cause

A load-order race in the `OnRep_ReplicatedHasBegunPlay` cascade that vanilla servers
don't expose because the replication burst is small enough that subsystems always land
first. Modded saves with many buildables and many subsystems widen the race window
enough that consumers lose it consistently. Different mods drive different consumers
(sinks, schematic walkers, …) so the visible crash changes depending on which mods are
loaded — each of those is the same root cause manifesting at a different vanilla call
site.

Confirmed not caused by:
- `SpaceElevatorSink` (disabled, still crashes)
- `FixClientResourceSinkPoints` (disabled, still crashes)
- Save state (older autosaves crash too)
- Launcher path (Steam vs SMM both crash)

## Solution

A small SML C++ mod that installs **one BEFORE-hook per known crash site**. Each hook
checks whether the relevant world subsystem has replicated; if not, it short-circuits
the original call in whatever way is safe for that API.

| Hook | Crash | Mitigation |
|------|-------|------------|
| `AFGBuildableResourceSink::BeginPlay` | Crash A | Cancel original, retry on a 0.1 s timer (5 s budget) until `AFGResourceSinkSubsystem` is non-null, then call original. |
| `UFGSchematic::CanGiveAccessToSchematic` | Crash B | Cancel original, return `false` when `AFGSchematicManager` is null. "Locked" is the safe default during the replication window — every caller already handles it. |

The cpp is structured so adding a 3rd / 4th hook is just another `InstallXHook()`
function plus one call from `StartupModule`. Same template as `FixClientResourceSinkPoints`
(Th3Fanbus' patch mod) — extended for the schematic case.

## What's in this folder

- `README.md` — this file
- `TOOLCHAIN_SETUP.md` — one-time Windows setup (Visual Studio + custom UE 5.3.2 + SF SDK)
- `BUILD_AND_TEST.md` — once toolchain is set up, this is the iterate-on-mod loop
- `Source/` — drop-in mod sources (uplugin + Build.cs + .h/.cpp)

## Order of operations

1. Boot into Windows
2. Follow `TOOLCHAIN_SETUP.md` end-to-end (one-time, ~4-6 hours mostly waiting on installs/compiles)
3. Follow `BUILD_AND_TEST.md` to drop in `Source/`, build, package, install, test
4. If it works → publish to ficsit.app so the next person doesn't hit this
5. If it doesn't → iterate on the hook strategy (notes in `BUILD_AND_TEST.md` § Fallback strategies)

## Why this approach over alternatives

- **Live-with-it / spam-rejoin:** rejected, the race lands maybe 1/4 attempts and you can't predict it
- **Bisect mods:** rejected, 121 mods × multi-minute test cycles = days, plus disabling mods may cause new save load errors that confuse the test
- **Binary-patch FactoryGame DLL:** rejected, breaks every game update and downstream code uses the null pointer anyway so we'd just crash 5 instructions later
- **Wait for community:** valid but unbounded; pursue in parallel as `BUILD_AND_TEST.md` § Publishing
