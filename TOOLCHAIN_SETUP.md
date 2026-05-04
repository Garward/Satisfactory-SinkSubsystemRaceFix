# Toolchain Setup (Windows, one-time)

Follow this in order. Each section is a checkpoint — if you have to stop, stop at a
section boundary so you know where to resume.

Disk: budget ~30 GB free for the engine + project + builds. (Older versions of this doc
said 60 GB because UE was compiled from source — that's no longer required.)

Total wall-clock: ~2–3 hours, mostly waiting on installs and shader compilation. The
historical 4–6 hour figure was from compiling UE; that step is gone.

> **Pre-staged in `E:\Games\Satisfactory\Modding\`:**
> - `vs_community.exe` — VS 2022 Community bootstrapper
> - `SML.vsconfig` — pre-baked workload selection
> - `SatisfactoryModLoader\` — Starter Project, already cloned
>
> Run `vs_community.exe` for §2, point it at `SML.vsconfig` for the workload picker.
> The other two files (UE-CSS installer, Wwise launcher) require authenticated
> downloads — instructions below.

## 1. Satisfactory installed and launched at least once

You already have it via Steam. Make sure it's on the **Stable** branch (not Experimental).
Right-click in Steam → Properties → Betas → "None — Stable". CL should be 463028.

## 2. Visual Studio 2022 Community

Run `E:\Games\Satisfactory\Modding\vs_community.exe`.

When the workload picker appears:
- Click the **More** dropdown at the top → **Import Configuration**
- Select `E:\Games\Satisfactory\Modding\SML.vsconfig`
- Click **Review details**, then **Install**

That `.vsconfig` selects exactly:
- Workloads: Desktop development with C++, Game development with C++
- Components: MSVC v143 v14.34-17.4 (Out of Support — but required, newer MSVC fails to
  link the engine), .NET 6.0 Runtime (Out of Support), .NET Framework 4.8.1 SDK

Install runs ~30–60 min depending on connection.

> **Why the "out of support" toolchain?** UE 5.3 was built against this specific MSVC
> ABI; newer toolchains break the link. You will see warnings about it; ignore them.

## 3. GitHub ↔ Epic Games account link (TWO steps, both required)

The custom UE installer lives in a private GitHub repo. You need both account links
done before you can download it.

### 3a. Standard Epic ↔ GitHub link

1. Make/use a GitHub account.
2. Go to https://www.unrealengine.com/en-US/ue-on-github and follow the link
   instructions.
3. Accept the email invite to the EpicGames GitHub org.
4. **Verify it worked** by visiting https://github.com/EpicGames-Mirror-A/UnrealEngine/
   — you should see a private repo, not 404.

If "Sorry, the service is temporarily unavailable" — disable adblocker, hard refresh.

### 3b. ficsit linker (separate, custom tool)

In addition to (3a), the SF modding community runs their own linker that grants access
to *their* fork of UE.

1. Visit https://linker.ficsit.app/link
2. Sign in with GitHub, follow the prompts.
3. After linking you'll be redirected to the engine repo — that's how you know it
   worked.
4. (Optional, after step 4 below succeeds) you can revoke the linker's permissions at
   https://github.com/settings/connections/applications/bdde02a7b3318bf2b84d

If you skip 3b you'll get a 404 in step 4 even though 3a is done.

## 4. Custom Unreal Engine — pre-built installer

> **No source compilation required.** The community ships a pre-built editor; total
> install is ~15–30 minutes vs. the 2–3 hours the old workflow needed.

1. Go to https://github.com/satisfactorymodding/UnrealEngine/releases/latest
   - If this 404s, you didn't finish step 3.
2. Download **all three** of these files into a single folder (e.g.
   `E:\Games\Satisfactory\Modding\UE-CSS-Installer\`):
   - `UnrealEngine-CSS-Editor-Win64.exe`
   - `UnrealEngine-CSS-Editor-Win64-1.bin`
   - `UnrealEngine-CSS-Editor-Win64-2.bin`
3. **Do not rename** the files — the installer locates the `.bin` parts by exact name.
4. Run the `.exe`. Default install location is `C:\Program Files\Unreal Engine - CSS\`
   — accept it.
5. (Optional) After install, run the VSIX in
   `C:\Program Files\Unreal Engine - CSS\Engine\Extras\UnrealVS\<VS year>\` to install
   the UnrealVS extension for Visual Studio. Lets you open `.cpp` files directly from
   the editor — nice but not required.

## 5. Linux cross-compile clang (optional — skip for our case)

Our server is Windows-under-Proton, not native Linux, so we only need `Win64` and
`WindowsServer` targets. Skip unless you later want to host on real Linux.

If you do want it: https://dev.epicgames.com/documentation/en-us/unreal-engine/linux-development-requirements-for-unreal-engine?application_version=5.3#nativetoolchain
— you need clang `-v22` (clang 16.0.6). Run installer, accept defaults.

## 6. Wwise

CSS uses Wwise for audio. Even though our mod has no audio, FactoryGame headers
transitively include Wwise headers — install is required.

### 6a. Audiokinetic Launcher

1. Go to https://www.audiokinetic.com/en/download/
2. Click "Download Audiokinetic Launcher"
3. Sign in / create a free Audiokinetic account (login is required).
4. Run the installer; it auto-launches the launcher.

### 6b. Install Wwise 2023.1.3.8471

> **Version is critical.** Older 2022.x doesn't support UE 5.3; newer than 2023.1.3.x
> has incompatible API changes. Old versions of this doc said `2022.1.x` — that is
> wrong for the current SF modding stack.

In the Launcher:
1. Left sidebar → **Wwise** (top entry, *not* "Wwise Audio Lab")
2. Under **INSTALL A NEW VERSION**, change `Latest` dropdown to **All**
3. Major: `2023.1` → Version: **`2023.1.3.8471`** → Install

When it shows the package picker, check **only**:
- Packages → Authoring, SDK (C++)
- Deployment Platforms → Linux
- Deployment Platforms → Microsoft → Windows: Visual Studio 2019, Visual Studio 2022,
  Game Core
- Plugins prompt → **Select None**

Don't *uncheck* anything that's already on by default.

## 7. Starter Project (SatisfactoryModLoader)

> **Already cloned to `E:\Games\Satisfactory\Modding\SatisfactoryModLoader\`.** If
> you're starting fresh, the clone command is:
>
> ```cmd
> cd /d E:\Games\Satisfactory\Modding
> git clone https://github.com/satisfactorymodding/SatisfactoryModLoader.git
> ```
>
> Stay on the default `master` branch — it tracks the latest stable game version per
> the repo's `repo-versions.adoc` config.
>
> **Don't move it under** Downloads, Desktop, Documents, OneDrive, or any path with
> non-ASCII characters or deep nesting — Unreal mishandles all of those.

Despite the name "SatisfactoryModLoader," this clone IS the starter UE project — it
contains FactoryGame headers, SML, Alpakit, and our mod plugin (once dropped in).

## 8. Wwise integration with the project (run inside the Launcher)

In the Audiokinetic Launcher:
1. Top-left tab: **Unreal Engine**
2. Don't click any "update" buttons there.
3. Click **Open other** under "Recent Unreal Engine Projects" → select
   `E:\Games\Satisfactory\Modding\SatisfactoryModLoader\FactoryGame.uproject`
4. Click **Integrate Wwise in Project...**
5. Set **Integration Version** dropdown from `Latest` to `All`. Then Major/Version
   dropdowns to **`2023.1.3.8471`** (suffixes like `.2970` are fine as long as it
   starts with that).
6. Under **Wwise Project**, click the dropdown triangle on the right → **New project**.
7. Click the blue **Integrate** button. Accept terms. Wait for "Operation completed
   successfully".

### 8a. Generate empty soundbanks (prevents editor crash later)

This produces a sister folder `SatisfactoryModLoader_WwiseProject\`.

1. Open `SatisfactoryModLoader_WwiseProject\SatisfactoryModLoader_WwiseProject.wproj`
   — the Wwise editor opens.
2. Project Explorer → SoundBanks tab.
3. Right-click the top folder → **Generate Soundbank(s) for all platforms**.
4. Wait for "Completed with message(s)" → Close.
5. Close Wwise editor and Launcher.

## 9. Generate Visual Studio project files

In Windows Explorer:
- Right-click `E:\Games\Satisfactory\Modding\SatisfactoryModLoader\FactoryGame.uproject`
  → **Generate Visual Studio project files** (Windows 11 may bury this under "Show
  more options").

If that errors, run from PowerShell:
```ps1
& "C:\Program Files\Unreal Engine - CSS\Engine\Build\BatchFiles\Build.bat" `
  -projectfiles `
  -project="E:\Games\Satisfactory\Modding\SatisfactoryModLoader\FactoryGame.uproject" `
  -game -rocket -progress
```

(In `cmd.exe`, drop the leading `&`.)

If it asks to pick an engine version, choose `5.3.2-CSS`. If `5.3.2-CSS` is missing or
flagged "binary build," there's a stale registry entry — open `regedit.exe`, navigate
to `HKEY_CURRENT_USER\SOFTWARE\Epic Games\Unreal Engine\Builds`, delete the bad
"Unreal Engine - CSS" key, then run
`C:\Program Files\Unreal Engine - CSS\SetupScripts\Register.bat` to re-register.

## 10. First Visual Studio build

1. Open `E:\Games\Satisfactory\Modding\SatisfactoryModLoader\FactoryGame.sln`.
2. **Dismiss popups** (in this order):
   - "These projects are either not supported..." → **OK**, ignore.
   - "Based on your solution, you might need to install extra components..." → ignore.
   - "Packages with vulnerabilities" → ignore (must use these exact versions).
   - "Unreal Engine Integration Configuration" tab → **uncheck "Show on startup"**,
     close it. **Do NOT enable** the "Visual Studio Integration Tool" or "Unreal Engine
     Test Adapter" — they break the build.
3. Top toolbar: select **Development Editor** + **Win64**.
4. **Do NOT** "Build → Build Solution". Instead:
   - Solution Explorer → expand `Games` → right-click **`FactoryGame`** → **Build**.
   - Also right-click `FactoryGame` → **Set as Startup Project** (one-time, helps the
     debugger later).
5. Compile takes 10–30 min depending on hardware. Watch the Output pane (View →
   Output) for progress.
6. Common build issues: see `BUILD_AND_TEST.md` § Common errors.

## 11. First open in Unreal Editor

After step 10 succeeds:
- Either double-click `FactoryGame.uproject`, or run
  `C:\Program Files\Unreal Engine - CSS\Engine\Binaries\Win64\UnrealEditor.exe` and
  browse to the project.
- First open compiles shaders — 15–30 min. Don't interrupt.
- If "GeneratedSoundBanks folder does not seem to be set" → click Yes, follow the
  prompted settings dialog (one-time fix).
- "New Plugins Are Available" → dismiss, the modding plugins are already enabled.
- Level Viewport errors about landscape rebuild → ignore.

## 12. Sanity check: package an empty mod (Alpakit)

Before writing our mod, verify the toolchain actually packages mods:

1. In the editor, top toolbar → **Alpakit Dev** (alpaca-in-a-box icon) → Open.
2. **Dev Packaging Settings** → expand **Windows** subheading:
   - Check **Enabled**
   - Check **Copy to Game Path** → `...` → select your Steam Satisfactory install
     folder (`C:\Program Files (x86)\Steam\steamapps\common\Satisfactory\`)
   - Check **Launch Game Type** → matching entry for that path
3. Click "Create Mod" (or use the existing `ExampleMod` in the list) → name it
   `SanityTest`, C++ template.
4. Click the **Alpakit!** button next to it.
5. Output goes to `SatisfactoryModLoader\Saved\ArchivedPlugins\SanityTest\` —
   verify a `.zip` exists and contains a `.pak`.

If both zip + pak exist → toolchain is working, proceed to `BUILD_AND_TEST.md`.

If you got compile errors here — common causes:
- Wrong MSVC version (need v14.34, not the default newest)
- Wwise integration didn't run cleanly (re-do step 8)
- Engine has stale registry entry (see step 9 troubleshooting)

## Resume points

- After §2: VS installed
- After §4: UE-CSS installer applied, editor binaries on disk
- After §6: Wwise installed
- After §7: starter project on disk
- After §8: Wwise integrated into the project
- After §10: editor compiled
- After §11: editor opens
- After §12: toolchain verified end-to-end, ready to drop in `SinkSubsystemRaceFix`
  (see `BUILD_AND_TEST.md`)
