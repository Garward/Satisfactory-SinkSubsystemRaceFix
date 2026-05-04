# Build, Install, and Test

Assumes `TOOLCHAIN_SETUP.md` is fully done (Alpakit packaged a SanityTest mod successfully).

## 1. Drop the mod into the project

This repository's layout is already the canonical UE plugin layout, so you can clone
it directly into your SML project's `Mods/` folder:

```cmd
cd "<your-SatisfactoryModLoader-path>\Mods"
git clone https://github.com/Garward/Satisfactory-SinkSubsystemRaceFix.git SinkSubsystemRaceFix
```

Or if you have the repo locally, copy everything:

```cmd
xcopy /E /I "<repo-root>" "<SatisfactoryModLoader>\Mods\SinkSubsystemRaceFix"
```

Resulting layout (UE / Alpakit expects exactly this):

```
<SatisfactoryModLoader>\Mods\SinkSubsystemRaceFix\
├── SinkSubsystemRaceFix.uplugin
└── Source\
    └── SinkSubsystemRaceFix\
        ├── SinkSubsystemRaceFix.Build.cs
        ├── Public\
        │   └── SinkSubsystemRaceFix.h
        └── Private\
            └── SinkSubsystemRaceFix.cpp
```

(The `.uplugin` lives in the mod root, **outside** the inner `Source/` folder. The
`Build.cs` lives in `Source/SinkSubsystemRaceFix/`. UBT is fussy about this layout.)

## 2. Regenerate project files

```cmd
:: right-click FactoryGame.uproject → Generate Visual Studio project files
:: OR from cmd:
"C:\UE5.3-SF\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe" -projectfiles -project="C:\SatisfactoryModLoader\FactoryGame.uproject" -game -engine
```

Open `FactoryGame.sln`. You should see `SinkSubsystemRaceFix` listed under
`Games/FactoryGame/Plugins/Mods/SinkSubsystemRaceFix`.

## 3. Build the editor target with the mod

In Visual Studio:
- Configuration: **Development Editor** | **Win64**
- Right-click the `FactoryGame` project → Build

If it builds clean → mod compiles. If it fails, fix and re-build before going further.
Common errors:
- `unresolved external symbol AFGBuildableResourceSink::...` → check `Build.cs`
  PublicDependencyModuleNames includes `FactoryGame`
- `cannot open include file 'Patching/NativeHookManager.h'` → SML missing from
  dependencies, add `"SML"` to PublicDependencyModuleNames
- `cannot open include file 'FGSchematicManager.h'` → header path differs in your
  CSS UE 5.3 build; try `"Schematics/FGSchematicManager.h"` or grep the SF SDK
  for the actual location.
- Schematic hook lambda parameter mismatch (e.g. `cannot deduce auto&` /
  `no matching overloaded function`) → `UFGSchematic::CanGiveAccessToSchematic`
  signature in your headers differs from the cpp's assumption of
  `(UObject* worldContext, TSubclassOf<UFGSchematic> inSchematic)`. Open
  `FGSchematic.h` and adjust the lambda parameter list to match the actual
  declaration. The macro `SUBSCRIBE_METHOD` infers the type from the function
  pointer, so the compile error will point to the lambda signature.

## 4. Package via Alpakit

In the editor:
1. Top toolbar → Alpakit → "Open Alpakit Dev"
2. Find `SinkSubsystemRaceFix` in the list, check it
3. Targets to enable: **Win64** (your client) and **WindowsServer** (for the Proton server)
4. Click "Package selected mods"

Output appears in `C:\SatisfactoryModLoader\Saved\ArchivedPlugins\SinkSubsystemRaceFix\`:
- `SinkSubsystemRaceFix-Win64.zip`
- `SinkSubsystemRaceFix-WindowsServer.zip`

Each contains a `.pak` (and possibly a `.utoc`/`.ucas` pair).

## 5. Install on the client

Easiest: use SMM/ficsit-cli to install from a local file.

```bash
# Linux side, after rebooting back:
ficsit install --target SatisfactoryWin <path-to>/SinkSubsystemRaceFix-Win64.zip
```

Or manually drop the contents into the client's mod folder:
`<Satisfactory client install>/FactoryGame/Mods/SinkSubsystemRaceFix/`

## 6. Install on the server

```bash
# stop server first
unzip SinkSubsystemRaceFix-WindowsServer.zip -d /home/garward/Games/Games/SatisfactoryWin/FactoryGame/Mods/SinkSubsystemRaceFix/
# start server
/home/garward/Games/Games/SatisfactoryWin/run-server.sh
```

Verify in `FactoryGame.log` (server-side) that the mod loaded:
```
LogSatisfactoryModLoader: Display: Loading mod 'SinkSubsystemRaceFix' v1.0.0
```

## 7. Test the join

1. Launch client (any path — Steam or SMM)
2. Multiplayer → Join Game → server IP
3. Watch `~/.steam/steam/steamapps/compatdata/526870/pfx/.../FactoryGame.log` (or
   wherever your client log lives) for either:
   - `LogSinkSubsystemRaceFix: Deferring AFGBuildableResourceSink::BeginPlay (subsystem not ready, attempt N)` — the hook fired and saved you
   - Successful join with no assertion → success
   - Assertion fires anyway → see Fallback strategies below

Do 5+ join attempts to get a confidence interval. Pre-fix the race lands ~1/4 attempts;
if all 5+ attempts join cleanly the fix is real.

## 8. Iteration loop

Quick edit-test cycle:
1. Edit `Source/Private/SinkSubsystemRaceFix.cpp`
2. Build in VS (Development Editor / Win64) — usually 10-30 seconds incremental
3. In editor → Alpakit → re-package (just SinkSubsystemRaceFix, just Win64+WindowsServer)
4. `ficsit install` re-runs (overwrites)
5. Restart server, restart client, retry

Don't bother re-launching the editor between iterations — Alpakit re-packages live.

## Fallback strategies (if the BEFORE hook approach doesn't fix it)

Listed in order of escalation. The base mod uses **strategy A**.

### A. BEFORE hook + retry timer (current implementation)
Hook `AFGBuildableResourceSink::BeginPlay` BEFORE original. If subsystem is null,
`Scope.Cancel()` the original and start a 0.1s timer that retries `BeginPlay` until
subsystem is non-null or `MAX_RETRIES` reached.

Risk: timer might fire after sink is GC'd. Code uses `IsValid(self)` guard.

### B. Hook the subsystem getter on the buildable
`AFGBuildableResourceSink` likely has a `GetResourceSinkSubsystem()` accessor it calls
in BeginPlay. Hook *that*, return a default-constructed-but-valid pointer (or trigger
a deferred-add to the subsystem when it appears). More invasive.

### C. Hook `AFGResourceSinkSubsystem::BeginPlay`
After the subsystem itself begins play on the client, walk all
`AFGBuildableResourceSink` actors that deferred and finish their setup. Pairs with A —
A defers, C completes. Use this if A's retry timer is unreliable.

### D. Hook `AGameStateBase::OnRep_ReplicatedHasBegunPlay` BEFORE
Block the cascade entirely until `AFGResourceSinkSubsystem::Get(GetWorld())` returns
non-null. Heavier hammer — delays *all* actors' BeginPlay, not just sinks. Use as last
resort; risks deadlock if subsystem itself depends on something downstream.

### E. CDO patch
Modify the sink's class default object so `BeginPlay` becomes a no-op when subsystem
is null, then trigger BeginPlay manually after subsystem ready. Only if A-D all fail.

## Publishing (optional but recommended)

If the mod works:
1. Bump `SemVersion` in `.uplugin` to `1.0.0`
2. Push source to a public GitHub repo
3. ficsit.app → Submit Mod → upload the WindowsServer + Win64 zips, link the repo
4. Tag the modset author (Ani's modpack) so the next person hitting this finds it

Short description for the listing: "Fixes the intermittent
`Assertion failed: mResourceSinkSubsystem [FGBuildableResourceSink.cpp:25]` client
crash when joining heavily-modded multiplayer servers. Defers Resource Sink BeginPlay
on the client until the subsystem has replicated."

## Removing the mod

If it doesn't help or makes things worse:
- Client: `ficsit uninstall SinkSubsystemRaceFix --target SatisfactoryWin`
- Server: `rm -rf /home/garward/Games/Games/SatisfactoryWin/FactoryGame/Mods/SinkSubsystemRaceFix/`

No save corruption risk — the mod adds no new content, only patches a function.
