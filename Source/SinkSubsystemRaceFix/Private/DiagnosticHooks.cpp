#include "SinkSubsystemRaceFix.h"

#include "FGSchematicManager.h"
#include "FGResourceSinkSubsystem.h"
#include "FGPlayerState.h"
#include "FGPlayerController.h"
#include "FGLightweightBuildableReplicationComponent.h"
#include "FGConveyorChainSubsystemReplicationComponent.h"
#include "FGRecipeManagerReplicationComponent.h"
#include "FGSchematicManagerReplicationComponent.h"
#include "Patching/NativeHookManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Containers/Ticker.h"
#include "UObject/ObjectKey.h"
#include "HAL/IConsoleManager.h"

// =============================================================================
// Diagnostic-only hooks. Zero behavioural change when CVar r.SinkRaceFix.Trace
// is 0 (the default). Toggle to 1 in console or [ConsoleVariables] in
// Engine.ini to start logging. Toggle to 2 for additional NetDriver state.
//
// What this gives us that the production hooks don't:
//   - Periodic state pulse (every 2s) of which subsystems are valid on each
//     world. First-seen transitions get a one-shot marker line so you can
//     read off the exact moment SchematicManager / ResourceSinkSubsystem
//     resolve client-side. If they never do, you see flat NULLs forever.
//   - Server-side dump on each AFGPlayerState::BeginPlay: subsystem
//     replication flags, dormancy, role, and NetDriver connection count
//     at the moment the new player joins.
//   - r.SinkRaceFix.DumpNow console command for one-shot inspection mid-hang.
// =============================================================================

namespace SinkRaceFixDiag
{
	// Default off for normal builds. Set `r.SinkRaceFix.Trace 1` in
	// Engine.ini's [ConsoleVariables] (or via console) when investigating a
	// join/replication regression.
	static TAutoConsoleVariable<int32> CVarTrace(
		TEXT("r.SinkRaceFix.Trace"),
		0,
		TEXT("SinkSubsystemRaceFix diagnostic verbosity. ")
		TEXT("0 = off, ")
		TEXT("1 = state ticker + per-join server dump + first-seen markers + [OBS] join-completion timeline, ")
		TEXT("2 = also NetDriver connection summary every tick."),
		ECVF_Default);

	static const float TICK_INTERVAL_SEC = 2.0f;

	static FTSTicker::FDelegateHandle GTickerHandle;
	static bool GbSchemMgrSeen = false;
	static bool GbSinkSubsysSeen = false;
	static bool GbGameStateSeen = false;

	static void DumpActor(const TCHAR* Label, AActor* A)
	{
		if (!IsValid(A))
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display, TEXT("[DIAG]   %s = NULL"), Label);
			return;
		}
		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("[DIAG]   %s name=%s replicates=%d alwaysRelevant=%d dormancy=%d localRole=%d remoteRole=%d"),
			Label, *A->GetName(),
			(int32)A->GetIsReplicated(),
			(int32)A->bAlwaysRelevant,
			(int32)A->NetDormancy,
			(int32)A->GetLocalRole(),
			(int32)A->GetRemoteRole());
	}

	static UWorld* FindGameWorld()
	{
		if (GEngine == nullptr) return nullptr;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::Game && Ctx.World() != nullptr)
			{
				return Ctx.World();
			}
		}
		return nullptr;
	}

	static const TCHAR* NetModeName(ENetMode NM)
	{
		switch (NM)
		{
			case NM_Client:           return TEXT("CLIENT");
			case NM_DedicatedServer:  return TEXT("DEDI");
			case NM_ListenServer:     return TEXT("LISTEN");
			default:                  return TEXT("STANDALONE");
		}
	}

	// =========================================================================
	// Join Observation: per-PC timeline of the natural server-side join
	// completion path. The four reliable-message replication components
	// (lightweight buildable / conveyor chain / recipe manager / schematic
	// manager) each go through the same lifecycle:
	//
	//   1. Component spawned within the joining PC -> BeginPlay() runs
	//   2. Component caches its subsystem (may need retries)
	//   3. Server-side: SendInitial*Message fires once -> bSentInitialMessage
	//   4. Reliable message arrives on client -> Received* runs ->
	//      bReceivedInitialReplicationMessage flips true (HasReceived* = true)
	//
	// We can hook the public BeginPlay AFTER on each, plus NotifyLoadedWorld
	// and OnPossess on the PC, then poll the public HasReceived* accessors
	// from the existing 2s state ticker to detect the flag-flip moment.
	//
	// Output is grep-able under [OBS][SERVER] / [OBS][CLIENT].
	// =========================================================================

	struct FJoinObsState
	{
		double NotifyLoadedWorldT      = -1.0;
		double OnPossessT              = -1.0;
		double LightweightBeginPlayT   = -1.0;
		double ConveyorChainBeginPlayT = -1.0;
		double RecipeMgrBeginPlayT     = -1.0;
		double SchematicMgrBeginPlayT  = -1.0;
		bool   LightweightFlagSeen     = false;
		bool   RecipeMgrFlagSeen       = false;
		bool   SchematicMgrFlagSeen    = false;
	};
	static TMap<FObjectKey, FJoinObsState> GJoinObs;

	static FJoinObsState& GetObs(const UObject* Key)
	{
		return GJoinObs.FindOrAdd(FObjectKey(Key));
	}

	static double WorldT(const UObject* O)
	{
		if (O == nullptr) return -1.0;
		const UWorld* W = O->GetWorld();
		return IsValid(W) ? W->GetTimeSeconds() : -1.0;
	}

	static APlayerController* OuterPCOf(const UObject* Component)
	{
		if (Component == nullptr) return nullptr;
		// Within=PlayerController on these components; outer chain ends in the PC.
		for (UObject* O = Component->GetOuter(); O != nullptr; O = O->GetOuter())
		{
			if (APlayerController* PC = Cast<APlayerController>(O))
			{
				return PC;
			}
		}
		return nullptr;
	}

	template<typename TComp>
	static void PollComponentFlag(
		APlayerController* PC,
		const TCHAR* Mode,
		const TCHAR* Label,
		bool FJoinObsState::* SeenFlag,
		double FJoinObsState::* BeginPlayT,
		bool (TComp::* HasReceived)() const)
	{
		if (PC == nullptr) return;
		TComp* Comp = PC->FindComponentByClass<TComp>();
		if (Comp == nullptr) return;

		FJoinObsState& S = GetObs(PC);
		const bool bReceived = (Comp->*HasReceived)();
		const bool bAlreadySeen = S.*SeenFlag;
		if (bReceived && !bAlreadySeen)
		{
			S.*SeenFlag = true;
			const double Now = WorldT(PC);
			const double BPT = S.*BeginPlayT;
			const double DeltaSec = (BPT >= 0.0) ? (Now - BPT) : -1.0;
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("[OBS][%s] %s flag FLIPPED PC=%s t=%.2f (deltaFromBeginPlay=%.2fs)"),
				Mode, Label, *PC->GetName(), Now, DeltaSec);
		}
	}

	static void PollJoinObsForWorld(UWorld* World, const TCHAR* Mode)
	{
		if (!IsValid(World)) return;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!IsValid(PC)) continue;

			PollComponentFlag<UFGLightweightBuildableReplicationComponent>(
				PC, Mode, TEXT("LightweightRep"),
				&FJoinObsState::LightweightFlagSeen,
				&FJoinObsState::LightweightBeginPlayT,
				&UFGLightweightBuildableReplicationComponent::HasReceivedInitialReplicationData);

			PollComponentFlag<UFGRecipeManagerReplicationComponent>(
				PC, Mode, TEXT("RecipeMgrRep"),
				&FJoinObsState::RecipeMgrFlagSeen,
				&FJoinObsState::RecipeMgrBeginPlayT,
				&UFGRecipeManagerReplicationComponent::HasReceivedInitialReplicationMessage);

			PollComponentFlag<UFGSchematicManagerReplicationComponent>(
				PC, Mode, TEXT("SchematicMgrRep"),
				&FJoinObsState::SchematicMgrFlagSeen,
				&FJoinObsState::SchematicMgrBeginPlayT,
				&UFGSchematicManagerReplicationComponent::HasReceivedInitialReplicationMessage);
		}
	}

	static bool StateTick(float /*DeltaTime*/)
	{
		const int32 Trace = CVarTrace.GetValueOnAnyThread();
		if (Trace < 1) return true; // keep ticker alive even when disabled

		UWorld* World = FindGameWorld();
		if (World == nullptr) return true;

		const TCHAR* Mode = NetModeName(World->GetNetMode());

		AFGSchematicManager*       SchemMgr   = AFGSchematicManager::Get(World);
		AFGResourceSinkSubsystem*  SinkSubsys = AFGResourceSinkSubsystem::Get(World);
		AGameStateBase*            GS         = World->GetGameState();
		APlayerController*         PC         = World->GetFirstPlayerController();
		APlayerState*              PS         = (PC != nullptr) ? PC->PlayerState : nullptr;

		// First-seen transitions — a single line that pins the exact second
		// each subsystem actually arrives on this world.
		if (!GbSchemMgrSeen && IsValid(SchemMgr))
		{
			GbSchemMgrSeen = true;
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("[DIAG][%s] SchematicManager FIRST RESOLVED at t=%.2f"),
				Mode, World->GetTimeSeconds());
		}
		if (!GbSinkSubsysSeen && IsValid(SinkSubsys))
		{
			GbSinkSubsysSeen = true;
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("[DIAG][%s] ResourceSinkSubsystem FIRST RESOLVED at t=%.2f"),
				Mode, World->GetTimeSeconds());
		}
		if (!GbGameStateSeen && IsValid(GS))
		{
			GbGameStateSeen = true;
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("[DIAG][%s] GameState FIRST RESOLVED at t=%.2f (class=%s)"),
				Mode, World->GetTimeSeconds(), *GS->GetClass()->GetName());
		}

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("[DIAG][%s t=%.1f] SchemMgr=%s SinkSubsys=%s GS=%s PC=%s PS=%s"),
			Mode, World->GetTimeSeconds(),
			IsValid(SchemMgr)   ? TEXT("ok") : TEXT("NULL"),
			IsValid(SinkSubsys) ? TEXT("ok") : TEXT("NULL"),
			IsValid(GS)         ? TEXT("ok") : TEXT("NULL"),
			IsValid(PC)         ? TEXT("ok") : TEXT("NULL"),
			IsValid(PS)         ? TEXT("ok") : TEXT("NULL"));

		if (Trace >= 2)
		{
			if (UNetDriver* ND = World->GetNetDriver())
			{
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("[DIAG][%s] NetDriver=%s ClientConns=%d ServerConn=%s"),
					Mode, *ND->GetName(), ND->ClientConnections.Num(),
					ND->ServerConnection != nullptr ? TEXT("yes") : TEXT("no"));
			}
		}

		// Poll public HasReceived* accessors on each PC's rep components and
		// log first-time flag flips with timing relative to BeginPlay. Quiet
		// at steady state — only logs on transitions.
		PollJoinObsForWorld(World, Mode);

		return true;
	}

	static void DumpAllSubsystems(UWorld* World, const TCHAR* Label)
	{
		if (!IsValid(World)) return;
		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("[DIAG] %s World=%s NetMode=%s t=%.2f"),
			Label, *World->GetName(), NetModeName(World->GetNetMode()),
			World->GetTimeSeconds());

		DumpActor(TEXT("SchematicManager"),      AFGSchematicManager::Get(World));
		DumpActor(TEXT("ResourceSinkSubsystem"), AFGResourceSinkSubsystem::Get(World));
		DumpActor(TEXT("GameState"),             World->GetGameState());

		if (UNetDriver* ND = World->GetNetDriver())
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("[DIAG]   NetDriver=%s ClientConnections=%d ServerConnection=%s"),
				*ND->GetName(), ND->ClientConnections.Num(),
				ND->ServerConnection != nullptr ? TEXT("yes") : TEXT("no"));
		}
	}

	static FAutoConsoleCommand CCmdDumpNow(
		TEXT("r.SinkRaceFix.DumpNow"),
		TEXT("One-shot dump of subsystem replication state on every game world."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			if (GEngine == nullptr) return;
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.WorldType != EWorldType::Game || Ctx.World() == nullptr) continue;
				DumpAllSubsystems(Ctx.World(), TEXT("DumpNow"));
			}
		}));

	void InstallDiagnosticHooks()
	{
		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Diagnostic hooks installed. Toggle with `r.SinkRaceFix.Trace 1` in console, ")
			TEXT("or via [ConsoleVariables] in Engine.ini. One-shot dump: `r.SinkRaceFix.DumpNow`."));

		// Stack a second AFTER subscriber on PlayerState::BeginPlay. This is
		// the same target Hook 3 uses; NHM allows multiple AFTER callbacks.
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerState, BeginPlay,
			[](AFGPlayerState* Self)
			{
				if (CVarTrace.GetValueOnAnyThread() < 1) return;
				if (!IsValid(Self)) return;
				if (Self->GetLocalRole() != ROLE_Authority) return;

				UWorld* World = Self->GetWorld();
				if (!IsValid(World)) return;

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("[DIAG][SERVER] PlayerState::BeginPlay (%s) — dumping subsystem state"),
					*Self->GetName());
				DumpAllSubsystems(World, TEXT("OnPlayerJoin"));
			});

		// ---------------------------------------------------------------
		// Join-observation AFTER hooks. All zero-side-effect: they only
		// stamp timestamps into the per-PC FJoinObsState map and log one
		// line per event. CVar-gated.
		// ---------------------------------------------------------------

		// Engine handoff: client tells server "world finished loading".
		// Server-side. This is the trigger for the rep components' tick to
		// start considering the client ready for initial messages.
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerController, NotifyLoadedWorld,
			[](AFGPlayerController* Self, FName WorldPackageName, bool bIsFinalDest)
			{
				if (CVarTrace.GetValueOnAnyThread() < 1) return;
				if (!IsValid(Self)) return;
				if (Self->GetLocalRole() != ROLE_Authority) return;
				FJoinObsState& S = GetObs(Self);
				const double T = WorldT(Self);
				S.NotifyLoadedWorldT = T;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("[OBS][SERVER] NotifyLoadedWorld PC=%s pkg=%s final=%d t=%.2f"),
					*Self->GetName(), *WorldPackageName.ToString(),
					(int32)bIsFinalDest, T);
			});

		// Server-side pawn possession — pairs with NotifyLoadedWorld.
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerController, OnPossess,
			[](AFGPlayerController* Self, APawn* InPawn)
			{
				if (CVarTrace.GetValueOnAnyThread() < 1) return;
				if (!IsValid(Self)) return;
				if (Self->GetLocalRole() != ROLE_Authority) return;
				FJoinObsState& S = GetObs(Self);
				const double T = WorldT(Self);
				S.OnPossessT = T;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("[OBS][SERVER] OnPossess PC=%s pawn=%s t=%.2f"),
					*Self->GetName(),
					IsValid(InPawn) ? *InPawn->GetName() : TEXT("NULL"), T);
			});

		// Replication component BeginPlay AFTER hooks. These run on both
		// server and client (the components exist on both). We stamp the
		// owning-PC's obs entry so the later poll can compute deltaFromBeginPlay.
		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGLightweightBuildableReplicationComponent, BeginPlay,
			[](UFGLightweightBuildableReplicationComponent* Self)
			{
				if (CVarTrace.GetValueOnAnyThread() < 1) return;
				if (!IsValid(Self)) return;
				APlayerController* PC = OuterPCOf(Self);
				if (PC == nullptr) return;
				const TCHAR* Mode = NetModeName(Self->GetWorld() != nullptr
					? Self->GetWorld()->GetNetMode() : NM_Standalone);
				FJoinObsState& S = GetObs(PC);
				const double T = WorldT(Self);
				S.LightweightBeginPlayT = T;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("[OBS][%s] LightweightRep BeginPlay PC=%s t=%.2f"),
					Mode, *PC->GetName(), T);
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGConveyorChainSubsystemReplicationComponent, BeginPlay,
			[](UFGConveyorChainSubsystemReplicationComponent* Self)
			{
				if (CVarTrace.GetValueOnAnyThread() < 1) return;
				if (!IsValid(Self)) return;
				APlayerController* PC = OuterPCOf(Self);
				if (PC == nullptr) return;
				const TCHAR* Mode = NetModeName(Self->GetWorld() != nullptr
					? Self->GetWorld()->GetNetMode() : NM_Standalone);
				FJoinObsState& S = GetObs(PC);
				const double T = WorldT(Self);
				S.ConveyorChainBeginPlayT = T;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("[OBS][%s] ConveyorChainRep BeginPlay PC=%s t=%.2f"),
					Mode, *PC->GetName(), T);
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGRecipeManagerReplicationComponent, BeginPlay,
			[](UFGRecipeManagerReplicationComponent* Self)
			{
				if (CVarTrace.GetValueOnAnyThread() < 1) return;
				if (!IsValid(Self)) return;
				APlayerController* PC = OuterPCOf(Self);
				if (PC == nullptr) return;
				const TCHAR* Mode = NetModeName(Self->GetWorld() != nullptr
					? Self->GetWorld()->GetNetMode() : NM_Standalone);
				FJoinObsState& S = GetObs(PC);
				const double T = WorldT(Self);
				S.RecipeMgrBeginPlayT = T;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("[OBS][%s] RecipeMgrRep BeginPlay PC=%s t=%.2f"),
					Mode, *PC->GetName(), T);
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGSchematicManagerReplicationComponent, BeginPlay,
			[](UFGSchematicManagerReplicationComponent* Self)
			{
				if (CVarTrace.GetValueOnAnyThread() < 1) return;
				if (!IsValid(Self)) return;
				APlayerController* PC = OuterPCOf(Self);
				if (PC == nullptr) return;
				const TCHAR* Mode = NetModeName(Self->GetWorld() != nullptr
					? Self->GetWorld()->GetNetMode() : NM_Standalone);
				FJoinObsState& S = GetObs(PC);
				const double T = WorldT(Self);
				S.SchematicMgrBeginPlayT = T;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("[OBS][%s] SchematicMgrRep BeginPlay PC=%s t=%.2f"),
					Mode, *PC->GetName(), T);
			});

		GTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&StateTick),
			TICK_INTERVAL_SEC);
	}

	void UninstallDiagnosticHooks()
	{
		if (GTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GTickerHandle);
			GTickerHandle.Reset();
		}
	}
}
