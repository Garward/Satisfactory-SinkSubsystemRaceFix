#include "SinkSubsystemRaceFix.h"

#include "Buildables/FGBuildableResourceSink.h"
#include "FGResourceSinkSubsystem.h"
#include "FGSchematic.h"
#include "AvailabilityDependencies/FGAvailabilityDependency.h"
#include "FGSchematicManager.h"
#include "FGRecipeManager.h"
#include "FGRecipeManagerReplicationComponent.h"
#include "FGSchematicManagerReplicationComponent.h"
#include "FGRecipe.h"
#include "FGCategory.h"
#include "FGGameState.h"
#include "FGBlueprintFunctionLibrary.h"
#include "FGGameMode.h"
#include "FGCharacterPlayer.h"
#include "FGPlayerController.h"
#include "FGPlayerState.h"
#include "FGLocalPlayer.h"
#include "FGBuildableSubsystem.h"
#include "FGLightweightBuildableSubsystem.h"
#include "FGLightweightBuildableReplicationComponent.h"
#include "FGLightweightBuildableReplicationTypes.h"
#include "Replication/FGReplicationGraph.h"
#include "AbstractInstanceManager.h"
#include "FGActorRepresentationManager.h"
#include "FGBlueprintSubsystem.h"
#include "FGGamePhaseManager.h"
#include "FGMapManager.h"
#include "FGTutorialIntroManager.h"
#include "FGConveyorChainSubsystem.h"
#include "FGConveyorItemSubSystem.h"
#include "FGConveyorChainActor.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Creature/FGCreature.h"
#include "FGItemPickup.h"
#include "FGItemPickup_Spawnable.h"
#include "UI/FGGameUI.h"
#include "Subsystem/ModSubsystem.h"
#include "Subsystem/SubsystemActorManager.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Patching/NativeHookManager.h"
#include "TimerManager.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameStateBase.h"
#include "Containers/Ticker.h"
#include "MoviePlayer.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ObjectKey.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWidget.h"

DEFINE_LOG_CATEGORY(LogSinkSubsystemRaceFix);

#define MOD_NAME TEXT("SinkSubsystemRaceFix")

// Sink-defer retry budget: 50 * 0.1s = 5s.
static const int32 SINK_MAX_RETRIES = 50;
static const float SINK_RETRY_INTERVAL = 0.1f;

namespace
{
	template<typename Tag, typename Tag::type Member>
	struct TPrivateMemberAccess
	{
		friend typename Tag::type GetPrivateMember(Tag)
		{
			return Member;
		}
	};

	struct FFGCharacterPlayerSetOnlineStateTag
	{
		using type = void (AFGCharacterPlayer::*)(const bool);
		friend type GetPrivateMember(FFGCharacterPlayerSetOnlineStateTag);
	};

	struct FFGCharacterPlayerIsOnlineTag
	{
		using type = TOptional<bool> AFGCharacterPlayer::*;
		friend type GetPrivateMember(FFGCharacterPlayerIsOnlineTag);
	};

	struct FFGPlayerStateCreateDefaultHotbarsTag
	{
		using type = void (AFGPlayerState::*)();
		friend type GetPrivateMember(FFGPlayerStateCreateDefaultHotbarsTag);
	};

	struct FFGPlayerStateOnRepPlayerHotbarsTag
	{
		using type = void (AFGPlayerState::*)();
		friend type GetPrivateMember(FFGPlayerStateOnRepPlayerHotbarsTag);
	};

	struct FFGPlayerStateUpdateActiveHotbarStateTag
	{
		using type = void (AFGPlayerState::*)();
		friend type GetPrivateMember(FFGPlayerStateUpdateActiveHotbarStateTag);
	};

	struct FFGSchematicReplicationManagerTag
	{
		using type = AFGSchematicManager* UFGSchematicManagerReplicationComponent::*;
		friend type GetPrivateMember(FFGSchematicReplicationManagerTag);
	};

	struct FFGSchematicReplicationRegisteredHandlerTag
	{
		using type = bool UFGSchematicManagerReplicationComponent::*;
		friend type GetPrivateMember(FFGSchematicReplicationRegisteredHandlerTag);
	};

	struct FFGSchematicReplicationInitialReceivedTag
	{
		using type = bool UFGSchematicManagerReplicationComponent::*;
		friend type GetPrivateMember(FFGSchematicReplicationInitialReceivedTag);
	};

	struct FFGRecipeReplicationManagerTag
	{
		using type = AFGRecipeManager* UFGRecipeManagerReplicationComponent::*;
		friend type GetPrivateMember(FFGRecipeReplicationManagerTag);
	};

	struct FFGRecipeReplicationPendingAvailableTag
	{
		using type = TArray<TSubclassOf<UFGRecipe>> UFGRecipeManagerReplicationComponent::*;
		friend type GetPrivateMember(FFGRecipeReplicationPendingAvailableTag);
	};

	struct FFGRecipeReplicationPendingRemovalTag
	{
		using type = TArray<TSubclassOf<UFGRecipe>> UFGRecipeManagerReplicationComponent::*;
		friend type GetPrivateMember(FFGRecipeReplicationPendingRemovalTag);
	};

	struct FFGRecipeReplicationInitialReceivedTag
	{
		using type = bool UFGRecipeManagerReplicationComponent::*;
		friend type GetPrivateMember(FFGRecipeReplicationInitialReceivedTag);
	};

	struct FFGPlayerControllerHasInitialLightweightDataTag
	{
		using type = bool (AFGPlayerController::*)() const;
		friend type GetPrivateMember(FFGPlayerControllerHasInitialLightweightDataTag);
	};

	struct FFGPlayerControllerIsLevelStreamingCompleteTag
	{
		using type = bool (AFGPlayerController::*)() const;
		friend type GetPrivateMember(FFGPlayerControllerIsLevelStreamingCompleteTag);
	};

	struct FFGPlayerControllerFinishRespawnTag
	{
		using type = void (AFGPlayerController::*)();
		friend type GetPrivateMember(FFGPlayerControllerFinishRespawnTag);
	};

	struct FFGPlayerControllerClientDoneRespawningTag
	{
		using type = void (AFGPlayerController::*)();
		friend type GetPrivateMember(FFGPlayerControllerClientDoneRespawningTag);
	};

	struct FFGLightweightPendingGameStateInstancesTag
	{
		using type = TMap<TSubclassOf<AFGBuildable>, TArray<FPendingGamestateRuntimeDataAdd>> AFGLightweightBuildableSubsystem::*;
		friend type GetPrivateMember(FFGLightweightPendingGameStateInstancesTag);
	};

	struct FFGAbstractInstanceManagerInstanceMapTag
	{
		using type = TMap<FName, FInstanceComponentData> AAbstractInstanceManager::*;
		friend type GetPrivateMember(FFGAbstractInstanceManagerInstanceMapTag);
	};

	// Re-applying the current onboarding step to the client UI when the
	// OnRep_CurrentOnboardingStep edge fires before the player controller
	// has bound its listener — common on heavy-mod saves where rep arrives
	// before the controller is fully spawned. Calling this directly with
	// the current step bypasses the missed-edge problem.
	struct FFGPlayerControllerNativeOnOnboardingStepUpdatedTag
	{
		using type = void (AFGPlayerController::*)(UFGOnboardingStep*);
		friend type GetPrivateMember(FFGPlayerControllerNativeOnOnboardingStepUpdatedTag);
	};

	struct FFGSchematicUnlocksTag
	{
		using type = TArray<class UFGUnlock*> UFGSchematic::*;
		friend type GetPrivateMember(FFGSchematicUnlocksTag);
	};

	struct FFGSchematicDependenciesTag
	{
		using type = TArray<class UFGAvailabilityDependency*> UFGSchematic::*;
		friend type GetPrivateMember(FFGSchematicDependenciesTag);
	};

	template struct TPrivateMemberAccess<FFGCharacterPlayerSetOnlineStateTag, &AFGCharacterPlayer::SetOnlineState>;
	template struct TPrivateMemberAccess<FFGCharacterPlayerIsOnlineTag, &AFGCharacterPlayer::mIsPlayerOnline>;
	template struct TPrivateMemberAccess<FFGPlayerStateCreateDefaultHotbarsTag, &AFGPlayerState::CreateDefaultHotbars>;
	template struct TPrivateMemberAccess<FFGPlayerStateOnRepPlayerHotbarsTag, &AFGPlayerState::OnRep_PlayerHotbars>;
	template struct TPrivateMemberAccess<FFGPlayerStateUpdateActiveHotbarStateTag, &AFGPlayerState::UpdateActiveHotbarState>;
	template struct TPrivateMemberAccess<FFGSchematicReplicationManagerTag, &UFGSchematicManagerReplicationComponent::mSchematicManager>;
	template struct TPrivateMemberAccess<FFGSchematicReplicationRegisteredHandlerTag, &UFGSchematicManagerReplicationComponent::bRegisteredMessageHandler>;
	template struct TPrivateMemberAccess<FFGSchematicReplicationInitialReceivedTag, &UFGSchematicManagerReplicationComponent::bHasReceivedInitialReplicationMessage>;
	template struct TPrivateMemberAccess<FFGRecipeReplicationManagerTag, &UFGRecipeManagerReplicationComponent::mRecipeManager>;
	template struct TPrivateMemberAccess<FFGRecipeReplicationPendingAvailableTag, &UFGRecipeManagerReplicationComponent::mPendingAvailableRecipes>;
	template struct TPrivateMemberAccess<FFGRecipeReplicationPendingRemovalTag, &UFGRecipeManagerReplicationComponent::mPendingRemovalRecipes>;
	template struct TPrivateMemberAccess<FFGRecipeReplicationInitialReceivedTag, &UFGRecipeManagerReplicationComponent::mHasReceivedInitialReplicationMessage>;
	template struct TPrivateMemberAccess<FFGPlayerControllerHasInitialLightweightDataTag, &AFGPlayerController::HasReceivedInitialLightweightReplicationData>;
	template struct TPrivateMemberAccess<FFGPlayerControllerIsLevelStreamingCompleteTag, &AFGPlayerController::IsLevelStreamingComplete>;
	template struct TPrivateMemberAccess<FFGPlayerControllerFinishRespawnTag, &AFGPlayerController::FinishRespawn>;
	template struct TPrivateMemberAccess<FFGPlayerControllerClientDoneRespawningTag, &AFGPlayerController::Server_ClientDoneRespawning>;
	template struct TPrivateMemberAccess<FFGLightweightPendingGameStateInstancesTag, &AFGLightweightBuildableSubsystem::mPendingGameStateInstances>;
	template struct TPrivateMemberAccess<FFGAbstractInstanceManagerInstanceMapTag, &AAbstractInstanceManager::InstanceMap>;
	template struct TPrivateMemberAccess<FFGPlayerControllerNativeOnOnboardingStepUpdatedTag, &AFGPlayerController::Native_OnOnboardingStepUpdated>;
	template struct TPrivateMemberAccess<FFGSchematicUnlocksTag, &UFGSchematic::mUnlocks>;
	template struct TPrivateMemberAccess<FFGSchematicDependenciesTag, &UFGSchematic::mSchematicDependencies>;

	// =========================================================================
	// Hook 1: AFGBuildableResourceSink::BeginPlay
	// Crash: Assertion failed: mResourceSinkSubsystem [FGBuildableResourceSink.cpp:25]
	// Cause: Sink BeginPlay runs on client before AFGResourceSinkSubsystem has
	//        replicated. Original code asserts on the null pointer.
	// Fix:   Cancel original BeginPlay, retry on a 0.1s timer until subsystem
	//        is non-null or budget exhausted.
	// =========================================================================
	void DeferredSinkBeginPlay(TWeakObjectPtr<AFGBuildableResourceSink> WeakSink, int32 Attempt);

	// Pre-populate the sink's UPROPERTY(Transient) cache field so the
	// `check(mResourceSinkSubsystem)` at FGBuildableResourceSink.cpp:25 passes
	// even if the original BeginPlay's internal lookup (closed source — likely
	// a different overload of AFGResourceSinkSubsystem::Get) returns null at
	// this exact moment in the load sequence.
	bool PopulateSinkSubsystemField(AFGBuildableResourceSink* Sink, AFGResourceSinkSubsystem* Subsystem)
	{
		FProperty* Prop = Sink->GetClass()->FindPropertyByName(TEXT("mResourceSinkSubsystem"));
		FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
		if (ObjProp == nullptr)
		{
			UE_LOG(LogSinkSubsystemRaceFix, Error,
				TEXT("mResourceSinkSubsystem property not found on %s (SDK changed?)"),
				*Sink->GetClass()->GetName());
			return false;
		}
		ObjProp->SetObjectPropertyValue_InContainer(Sink, Subsystem);
		return true;
	}

	void TrySinkBeginPlayAgain(TWeakObjectPtr<AFGBuildableResourceSink> WeakSink, int32 Attempt)
	{
		AFGBuildableResourceSink* Sink = WeakSink.Get();
		if (!IsValid(Sink))
		{
			return;
		}

		UWorld* World = Sink->GetWorld();
		if (!IsValid(World))
		{
			return;
		}

		AFGResourceSinkSubsystem* Subsystem = AFGResourceSinkSubsystem::Get(World);
		if (Subsystem != nullptr)
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("ResourceSinkSubsystem ready after %d retries; running deferred BeginPlay on %s"),
				Attempt, *Sink->GetName());
			PopulateSinkSubsystemField(Sink, Subsystem);
			Sink->BeginPlay();
			return;
		}

		if (Attempt >= SINK_MAX_RETRIES)
		{
			UE_LOG(LogSinkSubsystemRaceFix, Error,
				TEXT("ResourceSinkSubsystem still null after %d retries; running BeginPlay anyway on %s ")
				TEXT("(this will likely assert — investigate why subsystem never replicated)"),
				SINK_MAX_RETRIES, *Sink->GetName());
			Sink->BeginPlay();
			return;
		}

		DeferredSinkBeginPlay(WeakSink, Attempt + 1);
	}

	void DeferredSinkBeginPlay(TWeakObjectPtr<AFGBuildableResourceSink> WeakSink, int32 Attempt)
	{
		AFGBuildableResourceSink* Sink = WeakSink.Get();
		if (!IsValid(Sink))
		{
			return;
		}

		UWorld* World = Sink->GetWorld();
		if (!IsValid(World))
		{
			return;
		}

		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateLambda([WeakSink, Attempt]()
			{
				TrySinkBeginPlayAgain(WeakSink, Attempt);
			}),
			SINK_RETRY_INTERVAL,
			false);
	}

	void InstallSinkHook()
	{
		SUBSCRIBE_UOBJECT_METHOD(AFGBuildableResourceSink, BeginPlay,
			[](auto& Scope, AFGBuildableResourceSink* Self)
			{
				if (!IsValid(Self))
				{
					return;
				}

				// Server-authority path: subsystem always exists locally on the host.
				if (Self->HasAuthority())
				{
					return;
				}

				UWorld* World = Self->GetWorld();
				if (!IsValid(World))
				{
					return;
				}

				AFGResourceSinkSubsystem* Subsystem = AFGResourceSinkSubsystem::Get(World);
				if (Subsystem != nullptr)
				{
					// World subsystem exists, but the sink's local cache field
					// might still be null when original BeginPlay runs its own
					// lookup. Pre-populate so the line-25 assertion passes.
					PopulateSinkSubsystemField(Self, Subsystem);
					return;
				}

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Deferring AFGBuildableResourceSink::BeginPlay on %s (subsystem not ready)"),
					*Self->GetName());

				Scope.Cancel();
				DeferredSinkBeginPlay(TWeakObjectPtr<AFGBuildableResourceSink>(Self), 1);
			});
	}

	// =========================================================================
	// Hook 2: UFGSchematic::CanGiveAccessToSchematic
	// Crash: EXCEPTION_ACCESS_VIOLATION reading 0x2b8 in
	//        UFGSchematic::CanGiveAccessToSchematic [FGSchematic.cpp:408]
	// Cause: SML's content registry / mod blueprints (e.g. AdvancedRecipes
	//        iterating WasteShielding schematics) call this static helper
	//        during the BeginPlay cascade. The helper looks up
	//        AFGSchematicManager from the world; on the client it hasn't
	//        replicated yet and the dereference faults.
	//        Confirmed in log immediately before crash:
	//          LogMAMTips: Warning: Schematic Manager not Replicated yet.
	// Fix:   Short-circuit the call to return false when the manager is
	//        not yet available. "Player can't access yet" is the safe answer
	//        during the replication window — same effect as the schematic
	//        being locked, which all callers already handle.
	//
	// SIGNATURE NOTE: CSS UE 5.3 declares this as a static UFUNCTION. The
	// expected signature is:
	//     static bool UFGSchematic::CanGiveAccessToSchematic(
	//         UObject* worldContext, TSubclassOf<UFGSchematic> inSchematic);
	// If your headers show a different parameter order or set, adjust the
	// lambda parameter list to match. The hook macro infers the signature
	// from the function pointer, so a mismatch will surface as a compile
	// error in the lambda, not at runtime.
	// =========================================================================
	bool GetInitialManagerReplicationReady(AFGPlayerController* PC, bool& bOutRecipeReady, bool& bOutSchematicReady);

	bool IsSchematicAccessJoinUnsafe(UWorld* World)
	{
		if (!IsValid(World) || World->GetNetMode() != NM_Client)
		{
			return false;
		}

		AFGPlayerController* PC = Cast<AFGPlayerController>(World->GetFirstPlayerController());
		if (!IsValid(PC))
		{
			return true;
		}

		bool bRecipeInitial = false;
		bool bSchematicInitial = false;
		if (!GetInitialManagerReplicationReady(PC, bRecipeInitial, bSchematicInitial))
		{
			return true;
		}

		// Do not use PC->IsRespawning() as a schematic-access blocker here.
		// The respawn flag can legitimately stay true until Hook 17 completes
		// the join flow, and milestone/HUB UI may cache the false result as an
		// empty recipe/cost panel. Once recipe + schematic initial replication
		// have arrived, the vanilla access check is safe enough to run even
		// while the controller is still in the visual respawn/loading phase.
		return false;
	}

	void InstallSchematicHook()
	{
		SUBSCRIBE_METHOD(UFGSchematic::CanGiveAccessToSchematic,
			[](auto& Scope, TSubclassOf<UFGSchematic> InSchematic, UObject* WorldContext)
			{
				// Hardening (added after a heavy-mod join crashed at
				// FGSchematic.cpp:408 when the milestone UI was opened — the
				// schematic class iterated by GetAvailableSchematicsOfTypes
				// had a null entry in its mUnlocks array). The original
				// implementation's null-deref happens *inside* the function,
				// so we have to filter out bad inputs *before* it runs.

				// Step 1: schematic class itself must be loaded.
				UClass* SchemClass = *InSchematic;
				if (SchemClass == nullptr)
				{
					Scope.Override(false);
					return;
				}
				if (!IsValid(SchemClass) || !SchemClass->IsChildOf(UFGSchematic::StaticClass()))
				{
					static int32 InvalidClassLogBudget = 20;
					if (InvalidClassLogBudget > 0)
					{
						--InvalidClassLogBudget;
						UE_LOG(LogSinkSubsystemRaceFix, Display,
							TEXT("Hook 2: short-circuiting CanGiveAccessToSchematic for invalid schematic class %s"),
							IsValid(SchemClass) ? *SchemClass->GetName() : TEXT("NULL"));
					}
					Scope.Override(false);
					return;
				}

				// Step 2: CDO must be loaded — accessing it touches the same
				// pointer the original would.
				UFGSchematic* CDO = InSchematic.GetDefaultObject();
				if (!IsValid(CDO))
				{
					Scope.Override(false);
					return;
				}

				// Step 3: mUnlocks array must not contain null entries. This
				// is one of the candidates for the FGSchematic.cpp:408 AV on
				// heavy-mod saves: a mod schematic with a renamed/removed
				// UFGUnlock asset leaves a dangling null in the array.
				auto UnlocksMember = GetPrivateMember(FFGSchematicUnlocksTag{});
				const TArray<UFGUnlock*>& Unlocks = CDO->*UnlocksMember;
				for (UFGUnlock* U : Unlocks)
				{
					if (U == nullptr)
					{
						UE_LOG(LogSinkSubsystemRaceFix, Display,
							TEXT("Hook 2: short-circuiting CanGiveAccessToSchematic for %s — mUnlocks contains a null entry (mod asset removed/renamed)"),
							*SchemClass->GetName());
						Scope.Override(false);
						return;
					}
				}

				// Step 4: mSchematicDependencies must not contain null entries
				// either. This is the *other* candidate for the line-408 AV —
				// CanGiveAccessToSchematic iterates dependencies (when
				// mDependenciesBlocksSchematicAccess is true) and dereferences
				// each UFGAvailabilityDependency*. A renamed dependency asset
				// from a mod leaves the same dangling-null pattern.
				auto DepsMember = GetPrivateMember(FFGSchematicDependenciesTag{});
				const TArray<UFGAvailabilityDependency*>& Deps = CDO->*DepsMember;
				for (UFGAvailabilityDependency* D : Deps)
				{
					if (D == nullptr)
					{
						UE_LOG(LogSinkSubsystemRaceFix, Display,
							TEXT("Hook 2: short-circuiting CanGiveAccessToSchematic for %s — mSchematicDependencies contains a null entry (mod asset removed/renamed)"),
							*SchemClass->GetName());
						Scope.Override(false);
						return;
					}
				}

				// Step 5: existing checks — world + schematic manager must
				// be available before letting the original run.
				if (!IsValid(WorldContext))
				{
					Scope.Override(false);
					return;
				}

				UWorld* World = WorldContext->GetWorld();
				if (!IsValid(World))
				{
					Scope.Override(false);
					return;
				}

				// (Removed in v1.4.0) Step 6 was an AFGEventSubsystem null-guard
				// for the FGSchematic.cpp:408 race (offset 0x2b8 = mCurrentEvents.ArrayNum).
				// Hook 23 (AModSubsystem DispatchBeginPlay gate) prevents mod
				// Init() from running before AFGGameState::AreClientSubsystemsValid()
				// is true, which guarantees mEventSubsystem is non-null at the
				// point any mod-induced CanGiveAccessToSchematic call originates.
				// If a future call path bypasses Hook 23 and hits the same race,
				// re-add the null guard here.

				// Step 6: schematic manager must be available and the client
				// must not be in the early-join unsafe window.
				AFGSchematicManager* Manager = AFGSchematicManager::Get(World);
				if (IsValid(Manager) && !IsSchematicAccessJoinUnsafe(World))
				{
					return;
				}

				static int32 ManagerMissingLogBudget = 30;
				if (ManagerMissingLogBudget > 0)
				{
					--ManagerMissingLogBudget;
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Short-circuiting UFGSchematic::CanGiveAccessToSchematic ")
						TEXT("(SchematicManager not ready or client still joining); returning false"));
				}

				Scope.Override(false);
			});
	}

	// =========================================================================
	// Hook 3: AFGPlayerState::BeginPlay (server-only)
	// Symptom: Client connects, server logs
	//   "RegisterPlayerWithSession: Failed- UniqueId.IsValid(): true,
	//    IsV2(): true, IsOnline: false"
	//   followed by infinite-load on client (SchematicManager never replicates).
	// Cause:   Dedicated server build strips OnlineSubsystemSteam plugin and
	//          runs with NULL OSS. When a Steam client connects, the server's
	//          FUniqueNetIdRepl::IsOnline() returns false because no OSS is
	//          registered to validate the Steam-typed UniqueID. The check in
	//          AFGPlayerState::RegisterPlayerWithSession early-returns and
	//          leaves AFGPlayerState::mIsOnline = false. Subsystems gated on
	//          mIsOnline (including SchematicManager replication) skip this
	//          player => permanent hang.
	// Fix:     After AFGPlayerState::BeginPlay (server-side), force mIsOnline
	//          true via reflection and ForceNetUpdate. PlayerState is server-
	//          authoritative so the value replicates to clients.
	//
	// Why this hook target (not PostLogin): Earlier attempt hooked
	// AFGGameMode::PostLogin AFTER but the hook never fired in practice — the
	// active GameMode is BP_GameMode_C (Blueprint subclass) and the Blueprint
	// dispatch path bypasses native AFTER hooks on the C++ parent class.
	// AFGPlayerState::BeginPlay is a vanilla virtual on the leaf type and
	// fires reliably on the server.
	// =========================================================================
	void DeferredForceOnline(TWeakObjectPtr<AFGPlayerState> WeakPS, int32 Attempt);

	// Hook 12 retry helper. SF's PS->SetOnlineState(true) early-returns when
	// the PS hasn't cached its character yet ("Failed 1: false, false" log).
	// Retry every ~1s for ~10 attempts; after the character spawns, the call
	// will actually take effect and flip the master gating flag.
	static const int32 ONLINE_RETRY_MAX = 10;
	static const float ONLINE_RETRY_INTERVAL = 1.0f;

	void ScheduleOnlineStateRetries(TWeakObjectPtr<AFGPlayerState> WeakPS, int32 Attempt)
	{
		AFGPlayerState* PS = WeakPS.Get();
		if (!IsValid(PS)) return;
		UWorld* World = PS->GetWorld();
		if (!IsValid(World)) return;
		if (Attempt > ONLINE_RETRY_MAX) return;

		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateLambda([WeakPS, Attempt]()
			{
				AFGPlayerState* InnerPS = WeakPS.Get();
				if (!IsValid(InnerPS)) return;
				if (InnerPS->GetLocalRole() != ROLE_Authority) return;
				InnerPS->SetOnlineState(true);
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 12: retry %d/%d SetOnlineState(true) on %s"),
					Attempt, ONLINE_RETRY_MAX, *InnerPS->GetName());
				ScheduleOnlineStateRetries(WeakPS, Attempt + 1);
			}),
			ONLINE_RETRY_INTERVAL,
			false);
	}

	void TryForceOnline(TWeakObjectPtr<AFGPlayerState> WeakPS, int32 Attempt)
	{
		AFGPlayerState* PS = WeakPS.Get();
		if (!IsValid(PS))
		{
			return;
		}

		// Only the server should be writing to PlayerState fields.
		if (PS->GetLocalRole() != ROLE_Authority)
		{
			return;
		}

		FProperty* Prop = PS->GetClass()->FindPropertyByName(TEXT("mIsOnline"));
		FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop);
		if (BoolProp == nullptr)
		{
			UE_LOG(LogSinkSubsystemRaceFix, Error,
				TEXT("ForceOnline: mIsOnline property not found on %s (SDK changed?); skipping"),
				*PS->GetClass()->GetName());
			return;
		}

		const bool bWas = BoolProp->GetPropertyValue_InContainer(PS);
		if (bWas)
		{
			// Already true — vanilla path worked, nothing to fix.
			return;
		}

		BoolProp->SetPropertyValue_InContainer(PS, true);
		PS->ForceNetUpdate();

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Forced mIsOnline=true on %s (attempt %d) — bypassing ")
			TEXT("RegisterPlayerWithSession IsOnline:false on dedicated NULL-OSS server"),
			*PS->GetName(), Attempt);
	}

	void DeferredForceOnline(TWeakObjectPtr<AFGPlayerState> WeakPS, int32 Attempt)
	{
		AFGPlayerState* PS = WeakPS.Get();
		if (!IsValid(PS))
		{
			return;
		}
		UWorld* World = PS->GetWorld();
		if (!IsValid(World))
		{
			return;
		}
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateLambda([WeakPS, Attempt]()
			{
				TryForceOnline(WeakPS, Attempt);
			}),
			0.1f,
			false);
	}

	void InstallPlayerStateBeginPlayHook()
	{
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerState, BeginPlay,
			[](AFGPlayerState* Self)
			{
				if (!IsValid(Self))
				{
					return;
				}
				// Server-only — clients shouldn't write authoritative state.
				if (Self->GetLocalRole() != ROLE_Authority)
				{
					return;
				}

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("AFGPlayerState::BeginPlay fired for %s — scheduling force-online"),
					*Self->GetName());

				// First attempt immediately, plus a short defer in case
				// RegisterPlayerWithSession is called slightly later and
				// resets mIsOnline.
				TryForceOnline(TWeakObjectPtr<AFGPlayerState>(Self), 1);
				DeferredForceOnline(TWeakObjectPtr<AFGPlayerState>(Self), 2);

				// Hook 12: single-shot flip of the public master switch on
				// the PlayerState. Earlier retry-based version repeatedly
				// triggered SF's online-state-change side effects (likely
				// re-running RegisterPlayerWithSession or kicking) which
				// caused the connection to drop. One call is safer; if it
				// doesn't take effect immediately we accept the limitation.
				Self->SetOnlineState(true);
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 12: forced AFGPlayerState::SetOnlineState(true) on %s (single-shot)"),
					*Self->GetName());
			});
	}

	// =========================================================================
	// Hook 4: AFGPlayerState::RegisterPlayerWithSession (server-only)
	// Symptom: After Hook 3 forced mIsOnline=true, the IsOnline:false rejection
	//          stops firing, but a second SF gate still trips:
	//            "RegisterPlayerWithSession: LocalUser was Invalid."
	//          ULocalUserInfo is part of CSS's OnlineIntegration plugin and
	//          isn't created for connecting players when the dedicated server
	//          is using the NULL OSS — so this validation can never pass on
	//          dedicated, by structural design of the build.
	// Fix:     Cancel SF's whole RegisterPlayerWithSession override BEFORE it
	//          runs. The function appears to be a guard wrapper — it logs the
	//          two failure modes (UniqueId/IsOnline gate, LocalUser gate) and
	//          then proceeds to per-player setup that the dedicated server
	//          path doesn't actually need. Skipping it lets the rest of the
	//          login chain (PlayerController replication, RCO creation, etc.)
	//          proceed unblocked. As a safety, force mIsOnline=true here too
	//          in case Hook 3's BeginPlay fire didn't catch this PlayerState.
	// =========================================================================
	// =========================================================================
	// Hook 5: AFGSchematicManager::GetAvailableSchematicsOfTypes (AFTER)
	// Crash:   EXCEPTION_ACCESS_VIOLATION reading 0x0, observed when opening
	//          the HUB Terminal's "home shop" / learning-stage UI on a save
	//          that has stale schematic class references (mod removed/renamed
	//          its schematic asset; the save still has a TSubclassOf<UFGSchematic>
	//          that resolves to null at load).
	// Pattern: log immediately preceding the AV
	//            LogGame: Warning: GetRemainingCostFor failed, schematic was null.
	//            LogGame: Warning: FGSchematic::GetCost: class was nullpeter.
	//          Then UI iteration dereferences the null entry → AV at 0x0.
	// Fix:     Filter null entries out of the output array before any caller
	//          gets to iterate it. Eliminates the null-deref bug class at
	//          source, so all downstream paths (cost lookup, name display,
	//          dependency check) become safe — not just GetCost.
	// =========================================================================
	// =========================================================================
	// Hook 6: Subsystem replication kick (server-only)
	// Symptom: On heavily-modded saves with lots of placed content, the server
	//          opens 500+ actor channels for incoming connections (powerlines,
	//          buildables, creatures) but NEVER replicates the AlwaysRelevant
	//          subsystems (AFGSchematicManager, AFGResourceSinkSubsystem,
	//          AFGGameState). Client's Get(World) lookups stay null forever
	//          and the loading screen never completes. Confirmed from logs:
	//          server outgoing actor traffic for the joining connection
	//          consists almost entirely of buildables and PlayerController,
	//          with zero "Actor schematicManager" / "Actor BP_GameState_C"
	//          entries even after 47s of connection.
	// Cause:   Mostly speculative. Likely a combination of:
	//            (a) the rep graph filtering subsystems via streaming-level
	//                relevance checks while the client is still loading cells,
	//            (b) PlayerController's bloated initial state (mod-injected
	//                replicated arrays) gating subsequent actor processing.
	// Fix:     For each PlayerState BeginPlay (server), bump the 3 subsystems'
	//          NetUpdateFrequency to 100 Hz and ForceNetUpdate() repeatedly
	//          for 30s, giving the replication scheduler many extra chances
	//          to push them to the client.
	// =========================================================================
	static const int32 KICK_TICKS = 60;       // 60 * 0.5s = 30s
	static const float KICK_INTERVAL = 0.5f;

	// Conveyor chain actor wake loop intentionally removed.
	// Per the SDK headers (FGConveyorChainSubsystem.h), conveyor chain item
	// content is NOT carried by AFGConveyorChainActor's standard replication —
	// it's pulled by the client via Server_RequestChainItemUpdate /
	// Server_RequestChainSegmentData RPCs. Force-net-updating chain actors
	// every tick was therefore burning bandwidth without actually propagating
	// item state. The essentials for conveyor sync on a heavy-mod save are:
	//   1. AFGConveyorChainSubsystem replicates (carries mServerFactoryTickTime,
	//      the heartbeat that drives client-side IsUnusuallyLargeTickDelta).
	//      Already covered by KickSubsystemReplication's Bump(ConveyorChains).
	//   2. Client doesn't pause when the heartbeat is uneven on heavy saves —
	//      handled by Hook 14's passive IsUnusuallyLargeTickDelta -> false
	//      override.
	//   3. UFGConveyorChainSubsystemReplicationComponent::SendInitialReplicationData
	//      fires on each per-PC instance. Currently relies on natural BeginPlay
	//      flow; if real-world testing shows it's missing, we'll add a targeted
	//      hook (Option B from the analysis).

	// Time to wait between waking a pickup and putting it back to dormant.
	// Long enough for the initial replication burst to land on all clients,
	// short enough that we don't burn bandwidth re-replicating a static actor.
	static const float PICKUP_DORMANT_DELAY = 2.0f;

	void ForceWakeItemPickup(AFGItemPickup* Pickup)
	{
		if (!IsValid(Pickup)) return;
		if (!Pickup->HasAuthority()) return;

		// Brief wake-and-replicate window. NetUpdateFrequency is elevated only
		// for these few seconds; once the actor is dormant, frequency is moot
		// because UE skips replication entirely until something flushes the
		// dormancy.
		Pickup->NetUpdateFrequency = 30.0f;
		Pickup->MinNetUpdateFrequency = 10.0f;
		Pickup->FlushNetDormancy();
		Pickup->ForceNetUpdate();

		// Schedule transition back to fully dormant after the rep burst lands.
		// Pickups are nearly stateless once spawned (exist or destroyed); going
		// dormant means the server stops trying to replicate them every tick,
		// which is the bandwidth-bounded behavior we want for a heavy-mod save.
		// Channel close on Destroy() naturally re-replicates without us needing
		// to flush dormancy ourselves.
		UWorld* World = Pickup->GetWorld();
		if (!IsValid(World)) return;

		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateLambda([WeakPickup = TWeakObjectPtr<AFGItemPickup>(Pickup)]()
			{
				AFGItemPickup* P = WeakPickup.Get();
				if (!IsValid(P)) return;
				if (!P->HasAuthority()) return;
				P->SetNetDormancy(DORM_DormantAll);
			}),
			PICKUP_DORMANT_DELAY,
			false);
	}

	void KickSubsystemReplication(TWeakObjectPtr<UWorld> WeakWorld, int32 RemainingTicks)
	{
		UWorld* World = WeakWorld.Get();
		if (!IsValid(World)) return;

		AFGSchematicManager*      SchemMgr = AFGSchematicManager::Get(World);
		AFGResourceSinkSubsystem* Sink     = AFGResourceSinkSubsystem::Get(World);
		AGameStateBase*           GS       = World->GetGameState();
		AFGRecipeManager*         Recipe   = AFGRecipeManager::Get(World);
		AFGBuildableSubsystem*    Buildable = AFGBuildableSubsystem::Get(World);
		AFGLightweightBuildableSubsystem* Lightweight = AFGLightweightBuildableSubsystem::Get(World);
		AFGActorRepresentationManager* Repr = AFGActorRepresentationManager::Get(World);
		AFGBlueprintSubsystem* Blueprint = AFGBlueprintSubsystem::Get(World);
		AFGGamePhaseManager* GamePhase = AFGGamePhaseManager::Get(World);
		AFGMapManager* MapManager = AFGMapManager::Get(World);
		// AFGTutorialIntroManager carries mCurrentOnboardingStep (Replicated via
		// OnRep_CurrentOnboardingStep) — that's what populates the objective panel.
		// On heavy-mod saves it loses the rep race like the other subsystems.
		AFGTutorialIntroManager* TutorialIntro = AFGTutorialIntroManager::Get(World);
		AFGConveyorChainSubsystem* ConveyorChains = AFGConveyorChainSubsystem::Get(World);
		AFGConveyorItemSubsystem* ConveyorItems = AFGConveyorItemSubsystem::Get(World);

		auto Bump = [](AActor* A)
		{
			if (!IsValid(A)) return;
			A->NetUpdateFrequency = 100.0f;
			A->ForceNetUpdate();
		};
		Bump(SchemMgr);
		Bump(Sink);
		Bump(GS);
		Bump(Recipe);
		Bump(Buildable);
		Bump(Lightweight);
		Bump(Repr);
		Bump(Blueprint);
		Bump(GamePhase);
		Bump(MapManager);
		Bump(TutorialIntro);
		Bump(ConveyorChains);
		Bump(ConveyorItems);

		// SML mod subsystems with SpawnOnServer_Replicate policy. They race
		// with SF's vanilla subsystems for replication priority on heavy-mod
		// saves and can lose, leaving client-side hooks to dereference null
		// when the corresponding mod's subsystem accessor returns nullptr.
		// Concrete crash this fixes: InfiniteZoop's GetBaseCostMultiplier
		// hook fires during the build-gun auto-equip multicast that lands
		// in the same packet bundle as initial replication; if its
		// AInfiniteZoopSubsystem hasn't replicated yet, SetPublicZoopAmount
		// reads from null and crashes. Bumping all SpawnOnServer_Replicate
		// mod subsystems in the kick window closes the race for any mod
		// using this pattern (the SML standard).
		int32 ModSubsystemCount = 0;
		for (TActorIterator<AModSubsystem> It(World); It; ++It)
		{
			AModSubsystem* ModSub = *It;
			if (!IsValid(ModSub)) continue;
			if (ModSub->ReplicationPolicy != ESubsystemReplicationPolicy::SpawnOnServer_Replicate) continue;
			Bump(ModSub);
			++ModSubsystemCount;
		}
		if (RemainingTicks == KICK_TICKS)
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 6: bumping %d SML mod subsystem(s) with SpawnOnServer_Replicate policy"),
				ModSubsystemCount);
		}

		// DISABLED — experimental: this reflection sweep ForceNetUpdate'd 27
		// subsystem actors every 0.5s for 30s (~1620 forced replication pings)
		// during the join window. Hypothesis: it was clobbering the lightweight
		// buildable replication channel budget, leaving vanilla buildables
		// (space elevator, belts) and milestone state un-replicated. Commented
		// out to test — if vanilla buildables come back, the sweep is causing
		// more harm than good and a less aggressive replacement is needed.
		//
		// int32 GameStateSubsystemCount = 0;
		// if (IsValid(GS))
		// {
		// 	UClass* GSClass = GS->GetClass();
		// 	for (TFieldIterator<FObjectProperty> It(GSClass); It; ++It)
		// 	{
		// 		FObjectProperty* Prop = *It;
		// 		if (!(Prop->PropertyFlags & CPF_Net)) continue;
		// 		UObject* Value = Prop->GetObjectPropertyValue_InContainer(GS);
		// 		if (AActor* SubActor = Cast<AActor>(Value))
		// 		{
		// 			Bump(SubActor);
		// 			++GameStateSubsystemCount;
		// 		}
		// 	}
		// }
		// if (RemainingTicks == KICK_TICKS)
		// {
		// 	UE_LOG(LogSinkSubsystemRaceFix, Display,
		// 		TEXT("Hook 6: bumping %d replicated subsystem actor(s) on AFGGameState (reflection sweep)"),
		// 		GameStateSubsystemCount);
		// }

		if (RemainingTicks <= 1) return;

		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateLambda([WeakWorld, RemainingTicks]()
			{
				KickSubsystemReplication(WeakWorld, RemainingTicks - 1);
			}),
			KICK_INTERVAL,
			false);
	}

	// =========================================================================
	// Hook 8: UFGBlueprintFunctionLibrary::GetAvailableRecipesInCategory (BEFORE)
	//         + GetAvailableSubCategoriesForCategory (BEFORE)
	// Crash:  Assertion failed: recipeManager [FGBlueprintFunctionLibrary.cpp:1185]
	//         When the player opens the build gun on a freshly-joined client,
	//         AFGBuildGun::Equip → BP UI construction → BP_BuildMenu calls
	//         GetAvailableRecipesInCategory which dereferences AFGRecipeManager.
	//         If RecipeManager hasn't replicated yet (race window during the
	//         first second after join), the assert fires.
	// Fix:    Same pattern as Hook 2 (CanGiveAccessToSchematic). Cancel/override
	//         the call when RecipeManager is null. The build menu will show
	//         no recipes for ~1 second until replication catches up, then
	//         render normally — same effect as the schematic short-circuit.
	// =========================================================================
	void InstallRecipeManagerNullHook()
	{
		SUBSCRIBE_METHOD(UFGBlueprintFunctionLibrary::GetAvailableRecipesInCategory,
			[](auto& Scope, UObject* WorldContext, TSubclassOf<UFGCategory> /*Category*/,
			   TArray<TSubclassOf<UFGRecipe>>& OutRecipes)
			{
				if (!IsValid(WorldContext))
				{
					OutRecipes.Reset();
					Scope.Cancel();
					return;
				}
				UWorld* World = WorldContext->GetWorld();
				if (!IsValid(World))
				{
					OutRecipes.Reset();
					Scope.Cancel();
					return;
				}
				AFGRecipeManager* Mgr = AFGRecipeManager::Get(World);
				if (IsValid(Mgr))
				{
					return;
				}
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Short-circuiting GetAvailableRecipesInCategory ")
					TEXT("(RecipeManager not replicated yet); returning empty"));
				OutRecipes.Reset();
				Scope.Cancel();
			});

		SUBSCRIBE_METHOD(UFGBlueprintFunctionLibrary::GetAvailableSubCategoriesForCategory,
			[](auto& Scope, UObject* WorldContext, TSubclassOf<UFGCategory> /*Category*/,
			   TSubclassOf<UFGCategory> /*OutputSubCategoryClass*/)
			{
				if (!IsValid(WorldContext))
				{
					Scope.Override(TArray<TSubclassOf<UFGCategory>>());
					return;
				}
				UWorld* World = WorldContext->GetWorld();
				if (!IsValid(World))
				{
					Scope.Override(TArray<TSubclassOf<UFGCategory>>());
					return;
				}
				AFGRecipeManager* Mgr = AFGRecipeManager::Get(World);
				if (IsValid(Mgr))
				{
					return;
				}
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Short-circuiting GetAvailableSubCategoriesForCategory ")
					TEXT("(RecipeManager not replicated yet); returning empty"));
				Scope.Override(TArray<TSubclassOf<UFGCategory>>());
			});

		SUBSCRIBE_METHOD(UFGBlueprintFunctionLibrary::GetAvailableRecipesInSubCategory,
			[](auto& Scope, UObject* WorldContext, TSubclassOf<UFGCategory> /*Category*/,
			   TSubclassOf<UFGCategory> /*SubCategory*/, TArray<TSubclassOf<UFGRecipe>>& OutRecipes)
			{
				if (!IsValid(WorldContext) || !IsValid(WorldContext->GetWorld()) ||
					!IsValid(AFGRecipeManager::Get(WorldContext->GetWorld())))
				{
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Short-circuiting GetAvailableRecipesInSubCategory ")
						TEXT("(RecipeManager not replicated yet); returning empty"));
					OutRecipes.Reset();
					Scope.Cancel();
				}
			});

		SUBSCRIBE_METHOD(UFGBlueprintFunctionLibrary::GetAvailableRecipesWithDefaultMaterialInSubCategory,
			[](auto& Scope, APlayerController* PlayerController, TSubclassOf<UFGCategory> /*Category*/,
			   TSubclassOf<UFGCategory> /*SubCategory*/, TArray<TSubclassOf<UFGRecipe>>& OutRecipes)
			{
				if (!IsValid(PlayerController) || !IsValid(PlayerController->GetWorld()) ||
					!IsValid(AFGRecipeManager::Get(PlayerController->GetWorld())))
				{
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Short-circuiting GetAvailableRecipesWithDefaultMaterialInSubCategory ")
						TEXT("(RecipeManager not replicated yet); returning empty"));
					OutRecipes.Reset();
					Scope.Cancel();
				}
			});

		SUBSCRIBE_METHOD(UFGBlueprintFunctionLibrary::GetAvailableRecipesForMaterialDescriptorInSubCategory,
			[](auto& Scope, APlayerController* PlayerController, TSubclassOf<UFGCategory> /*Category*/,
			   TSubclassOf<UFGCategory> /*SubCategory*/,
			   TSubclassOf<class UFGFactoryCustomizationDescriptor_Material> /*MaterialDesc*/,
			   TArray<TSubclassOf<UFGRecipe>>& OutRecipes)
			{
				if (!IsValid(PlayerController) || !IsValid(PlayerController->GetWorld()) ||
					!IsValid(AFGRecipeManager::Get(PlayerController->GetWorld())))
				{
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Short-circuiting GetAvailableRecipesForMaterialDescriptorInSubCategory ")
						TEXT("(RecipeManager not replicated yet); returning empty"));
					OutRecipes.Reset();
					Scope.Cancel();
				}
			});

		// Same race in the dismantle peek path — when the build gun ticks
		// over a buildable, it queries the recipe manager for customization
		// recipes via GetCustomizationRecipeFromDesc.  If the manager hasn't
		// finished replicating its internal data yet (BeginPlay not fired
		// on the client), it AVs at offset 0x2f8 (uninitialized member).
		// Use HasActorBegunPlay() as the readiness proxy — once BeginPlay
		// runs, internal state is populated and normal calls go through.
		SUBSCRIBE_UOBJECT_METHOD(AFGRecipeManager, GetCustomizationRecipeFromDesc,
			[](auto& Scope, AFGRecipeManager* Self,
			   TSubclassOf<class UFGFactoryCustomizationDescriptor> /*Desc*/)
			{
				if (!IsValid(Self) || !Self->HasActorBegunPlay())
				{
					UE_LOG(LogSinkSubsystemRaceFix, Verbose,
						TEXT("Short-circuiting AFGRecipeManager::GetCustomizationRecipeFromDesc ")
						TEXT("(manager not yet initialized); returning empty"));
					Scope.Override(TSubclassOf<class UFGCustomizationRecipe>());
					return;
				}
			});
	}

	// =========================================================================
	// Hook 9: UCharacterMovementComponent::SetMovementMode (BEFORE)
	// Symptom: After joining, the local player's MovementMode is being set
	//          back to MOVE_None every tick by some other system (SF's
	//          "loading-state" code that never properly exits). Our timer-
	//          based restore in Hook 7 fires at 0.5s but loses the race.
	//          Also: deleting the floor under a frozen character leaves
	//          them hovering in mid-air — clear evidence that physics
	//          itself is disabled, not just movement input.
	// Fix:     Intercept every SetMovementMode call. If a caller attempts
	//          to set MOVE_None on the local autonomous-proxy character,
	//          cancel the call. Other characters and legitimate MOVE_None
	//          transitions on simulated proxies / non-owned pawns pass
	//          through normally.
	// =========================================================================
	void InstallMovementModeBlockHook()
	{
		SUBSCRIBE_UOBJECT_METHOD(UCharacterMovementComponent, SetMovementMode,
			[](auto& Scope, UCharacterMovementComponent* Self,
			   EMovementMode NewMovementMode, uint8 /*NewCustomMode*/)
			{
				if (!IsValid(Self)) return;
				ACharacter* Char = Self->GetCharacterOwner();
				if (!IsValid(Char) || !Char->IsPlayerControlled()) return;

				UE_LOG(LogSinkSubsystemRaceFix, Verbose,
					TEXT("Hook 9: SetMovementMode(%d) on player %s role=%d"),
					(int32)NewMovementMode, *Char->GetName(),
					(int32)Char->GetLocalRole());

				if (NewMovementMode == MOVE_None)
				{
					Scope.Cancel();
				}
			});

		// DisableMovement() either calls SetMovementMode(None) (caught above)
		// or sets MovementMode field directly when CharacterOwner is null.
		// Hook it BEFORE and cancel for player-owned components.
		SUBSCRIBE_UOBJECT_METHOD(UCharacterMovementComponent, DisableMovement,
			[](auto& Scope, UCharacterMovementComponent* Self)
			{
				if (!IsValid(Self)) return;
				ACharacter* Char = Self->GetCharacterOwner();
				if (!IsValid(Char) || !Char->IsPlayerControlled()) return;

				UE_LOG(LogSinkSubsystemRaceFix, Verbose,
					TEXT("Hook 9: Blocked DisableMovement() on player %s"),
					*Char->GetName());
				Scope.Cancel();
			});

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 9: SetMovementMode + DisableMovement intercepts installed"));
	}

	// =========================================================================
	// Hook 10: AFGCharacterPlayer::UpdateMovementModeOnRespawn (BEFORE)
	// Symptom: After Hook 9 successfully prevents MovementMode from going to
	//          None, character is STILL frozen — meaning the freeze involves
	//          more than just MovementMode (probably velocity zeroing, input
	//          flag, or pawn flag set in the same caller).
	// Theory:  AFGCharacterPlayer has UpdateMovementModeOnRespawn(bool
	//          bIsRespawning). When bIsRespawning is true, this function
	//          does the whole "freeze player for respawn animation" — sets
	//          MovementMode to None AND probably sets other state. The
	//          dismissal flag is broken on this save so it stays true.
	// Fix:     Cancel the call when bIsRespawning is true on a player
	//          character. The character stays in normal Walking state.
	// =========================================================================
	void InstallRespawnFreezeBlockHook()
	{
		SUBSCRIBE_UOBJECT_METHOD(AFGCharacterPlayer, UpdateMovementModeOnRespawn,
			[](auto& Scope, AFGCharacterPlayer* Self, bool bIsRespawning)
			{
				static int32 LogBudget = 20;
				if (LogBudget > 0)
				{
					LogBudget--;
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Hook 10: UpdateMovementModeOnRespawn(bIsRespawning=%d) on %s role=%d"),
						(int32)bIsRespawning,
						IsValid(Self) ? *Self->GetName() : TEXT("<null>"),
						IsValid(Self) ? (int32)Self->GetLocalRole() : -1);
				}
				if (bIsRespawning && IsValid(Self))
				{
					Scope.Cancel();
				}
			});

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 10: respawn-freeze intercept installed"));
	}

	// =========================================================================
	// Hook 12: AFGPlayerState::SetOnlineState (BEFORE) + force-call AFTER BeginPlay
	// AFGPlayerState (NOT AFGCharacterPlayer) has a PUBLIC SetOnlineState.
	// SF's flow that logs "Set Online State For Player : false" is on this
	// outer wrapper — fails when GameSession is invalid (NULL OSS dedicated).
	// Pattern from Th3Fanbus's FixClientResourceSinkPoints: re-do the setup
	// the engine skipped because of a broken precondition.
	//
	//   (a) Intercept SetOnlineState(false) — cancel; SF's transient false-set
	//       due to invalid session no longer flips the flag off.
	//   (b) After AFGPlayerState BeginPlay (server side) and again when our
	//       Hook 4 cancel fires, manually call SetOnlineState(true) so the
	//       flag is set even if SF's normal path never reached it.
	// =========================================================================
	void InstallOnlineStateHook()
	{
		SUBSCRIBE_UOBJECT_METHOD(AFGPlayerState, SetOnlineState,
			[](auto& Scope, AFGPlayerState* Self, bool isPlayerOnline)
			{
				if (!IsValid(Self)) return;
				static int32 LogBudget = 30;
				if (LogBudget > 0)
				{
					LogBudget--;
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Hook 12: AFGPlayerState::SetOnlineState(%d) on %s role=%d"),
						(int32)isPlayerOnline, *Self->GetName(),
						(int32)Self->GetLocalRole());
				}
				if (!isPlayerOnline)
				{
					Scope.Cancel();
				}
			});
		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 12: AFGPlayerState::SetOnlineState intercept installed"));
	}

	// =========================================================================
	// Hook 13: join/respawn completion repair
	// Current failure mode after subsystem replication is fixed:
	//   - Controller exists and action input fires.
	//   - CharacterMovement is forced back to Walking.
	//   - WASD/jump/physics/hotbar/build UI stay dead.
	//
	// The FactoryGame headers show the real join path is controller respawn /
	// level-streaming driven, with a character-level online switch:
	//   AFGPlayerController::NotifyLoadedWorld(...)
	//   AFGPlayerController::mIsRespawning / mRespawnFromJoin
	//   AFGCharacterPlayer::SetOnlineState(bool)
	//   AFGCharacterPlayer::mIsPlayerOnline
	//
	// Avoid more AFGPlayerState::SetOnlineState retries. That public wrapper
	// re-enters session/registration side effects and kicked clients during
	// testing. Instead, once possession is valid, repair the character-level
	// active state. Do not cancel/replay NotifyLoadedWorld or clear input gates
	// while the controller is respawning: that path drives the welcome splash,
	// objective setup, and several mod init hooks.
	// =========================================================================
	AFGCharacterPlayer* GetFGCharacterFromController(AFGPlayerController* PC)
	{
		return IsValid(PC) ? Cast<AFGCharacterPlayer>(PC->GetPawn()) : nullptr;
	}

	bool GetInitialManagerReplicationReady(AFGPlayerController* PC, bool& bRecipeReady, bool& bSchematicReady)
	{
		bRecipeReady = false;
		bSchematicReady = false;
		if (!IsValid(PC)) return false;

		if (const UFGRecipeManagerReplicationComponent* RecipeReplication = PC->FindComponentByClass<UFGRecipeManagerReplicationComponent>())
		{
			bRecipeReady = RecipeReplication->HasReceivedInitialReplicationMessage();
		}
		if (const UFGSchematicManagerReplicationComponent* SchematicReplication = PC->FindComponentByClass<UFGSchematicManagerReplicationComponent>())
		{
			bSchematicReady = SchematicReplication->HasReceivedInitialReplicationMessage();
		}

		return bRecipeReady && bSchematicReady;
	}

	const TCHAR* NetModeName(ENetMode NetMode)
	{
		switch (NetMode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}

	bool AreJoinCriticalObjectsReady(AFGPlayerController* PC);

	FString BuildManagerReplicationState(AFGPlayerController* PC)
	{
		if (!IsValid(PC)) return TEXT("<invalid-pc>");
		UWorld* World = PC->GetWorld();
		const ENetMode NetMode = IsValid(World) ? World->GetNetMode() : NM_Standalone;

		const UFGSchematicManagerReplicationComponent* SchemComp = PC->FindComponentByClass<UFGSchematicManagerReplicationComponent>();
		const UFGRecipeManagerReplicationComponent* RecipeComp = PC->FindComponentByClass<UFGRecipeManagerReplicationComponent>();
		auto HasInitialLightweightDataFn = GetPrivateMember(FFGPlayerControllerHasInitialLightweightDataTag{});
		auto IsLevelStreamingCompleteFn = GetPrivateMember(FFGPlayerControllerIsLevelStreamingCompleteTag{});
		const bool bLevelStreamingComplete = (PC->*IsLevelStreamingCompleteFn)();
		const bool bLightweightInitial = (PC->*HasInitialLightweightDataFn)();

		bool bSchemInitial = false;
		bool bSchemRegistered = false;
		bool bSchemManager = false;
		if (IsValid(SchemComp))
		{
			auto SchemInitialMember = GetPrivateMember(FFGSchematicReplicationInitialReceivedTag{});
			auto SchemRegisteredMember = GetPrivateMember(FFGSchematicReplicationRegisteredHandlerTag{});
			auto SchemManagerMember = GetPrivateMember(FFGSchematicReplicationManagerTag{});
			bSchemInitial = SchemComp->*SchemInitialMember;
			bSchemRegistered = SchemComp->*SchemRegisteredMember;
			bSchemManager = IsValid(SchemComp->*SchemManagerMember);
		}

		bool bRecipeInitial = false;
		bool bRecipeManager = false;
		int32 PendingAvailable = -1;
		int32 PendingRemoval = -1;
		if (IsValid(RecipeComp))
		{
			auto RecipeInitialMember = GetPrivateMember(FFGRecipeReplicationInitialReceivedTag{});
			auto RecipeManagerMember = GetPrivateMember(FFGRecipeReplicationManagerTag{});
			auto PendingAvailableMember = GetPrivateMember(FFGRecipeReplicationPendingAvailableTag{});
			auto PendingRemovalMember = GetPrivateMember(FFGRecipeReplicationPendingRemovalTag{});
			bRecipeInitial = RecipeComp->*RecipeInitialMember;
			bRecipeManager = IsValid(RecipeComp->*RecipeManagerMember);
			PendingAvailable = (RecipeComp->*PendingAvailableMember).Num();
			PendingRemoval = (RecipeComp->*PendingRemovalMember).Num();
		}

		return FString::Printf(
			TEXT("net=%s PC=%s PS=%s pawn=%s respawning=%d ready=%d levelStreaming=%d lightweightInitial=%d ")
			TEXT("schemComp=%d schemMgr=%d schemRegistered=%d schemInitial=%d ")
			TEXT("recipeComp=%d recipeMgr=%d recipeInitial=%d recipePendingAdd=%d recipePendingRemove=%d"),
			NetModeName(NetMode),
			*PC->GetName(),
			IsValid(PC->PlayerState) ? *PC->PlayerState->GetName() : TEXT("NULL"),
			IsValid(PC->GetPawn()) ? *PC->GetPawn()->GetName() : TEXT("NULL"),
			(int32)PC->IsRespawning(),
			(int32)AreJoinCriticalObjectsReady(PC),
			(int32)bLevelStreamingComplete,
			(int32)bLightweightInitial,
			(int32)IsValid(SchemComp),
			(int32)bSchemManager,
			(int32)bSchemRegistered,
			(int32)bSchemInitial,
			(int32)IsValid(RecipeComp),
			(int32)bRecipeManager,
			(int32)bRecipeInitial,
			PendingAvailable,
			PendingRemoval);
	}

	void LogManagerReplicationState(AFGPlayerController* PC, const TCHAR* Reason)
	{
		if (!IsValid(PC)) return;
		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 16: %s %s"),
			Reason,
			*BuildManagerReplicationState(PC));
	}

	bool AreRespawnCompletionPredicatesReady(AFGPlayerController* PC)
	{
		if (!IsValid(PC)) return false;
		if (!AreJoinCriticalObjectsReady(PC)) return false;

		auto HasInitialLightweightDataFn = GetPrivateMember(FFGPlayerControllerHasInitialLightweightDataTag{});
		auto IsLevelStreamingCompleteFn = GetPrivateMember(FFGPlayerControllerIsLevelStreamingCompleteTag{});
		return (PC->*IsLevelStreamingCompleteFn)() && (PC->*HasInitialLightweightDataFn)();
	}

	bool AreJoinCriticalObjectsReady(AFGPlayerController* PC)
	{
		if (!IsValid(PC)) return false;
		UWorld* World = PC->GetWorld();
		if (!IsValid(World)) return false;
		if (!IsValid(World->GetGameState())) return false;
		if (!IsValid(PC->PlayerState)) return false;
		if (!IsValid(GetFGCharacterFromController(PC))) return false;
		if (!IsValid(AFGSchematicManager::Get(World))) return false;
		if (!IsValid(AFGResourceSinkSubsystem::Get(World))) return false;
		if (!IsValid(AFGRecipeManager::Get(World))) return false;
		if (!IsValid(AFGBuildableSubsystem::Get(World))) return false;
		if (!IsValid(AFGLightweightBuildableSubsystem::Get(World))) return false;
		if (!IsValid(AFGActorRepresentationManager::Get(World))) return false;
		if (!IsValid(AFGMapManager::Get(World))) return false;
		if (!IsValid(AFGConveyorChainSubsystem::Get(World))) return false;
		if (!IsValid(AFGConveyorItemSubsystem::Get(World))) return false;
		if (World->GetNetMode() == NM_Client)
		{
			bool bRecipeInitial = false;
			bool bSchematicInitial = false;
			if (!GetInitialManagerReplicationReady(PC, bRecipeInitial, bSchematicInitial)) return false;
		}
		return true;
	}

	bool GetCharacterOnline(const AFGCharacterPlayer* Char)
	{
		if (!IsValid(Char)) return false;
		auto IsOnlineMember = GetPrivateMember(FFGCharacterPlayerIsOnlineTag{});
		const TOptional<bool>& Online = Char->*IsOnlineMember;
		return Online.IsSet() ? Online.GetValue() : false;
	}

	void RepairPlayerStateHotbars(AFGPlayerState* PS, const TCHAR* Reason)
	{
		if (!IsValid(PS)) return;
		if (!PS->HasAuthority()) return;
		if (PS->GetNumHotbars() > 0 && IsValid(PS->GetActiveHotbar())) return;

		const int32 BeforeNum = PS->GetNumHotbars();
		const int32 BeforeIndex = PS->GetActiveHotbarIndex();

		auto CreateDefaultHotbarsFn = GetPrivateMember(FFGPlayerStateCreateDefaultHotbarsTag{});
		auto OnRepPlayerHotbarsFn = GetPrivateMember(FFGPlayerStateOnRepPlayerHotbarsTag{});
		auto UpdateActiveHotbarStateFn = GetPrivateMember(FFGPlayerStateUpdateActiveHotbarStateTag{});

		(PS->*CreateDefaultHotbarsFn)();
		if (PS->GetNumHotbars() > 0 && PS->GetActiveHotbarIndex() < 0)
		{
			PS->SetHotbarIndex(0);
		}
		(PS->*OnRepPlayerHotbarsFn)();
		(PS->*UpdateActiveHotbarStateFn)();
		PS->ForceNetUpdate();

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 13: repaired PlayerState hotbars via %s on %s num %d->%d index %d->%d active=%s"),
			Reason,
			*PS->GetName(),
			BeforeNum,
			PS->GetNumHotbars(),
			BeforeIndex,
			PS->GetActiveHotbarIndex(),
			IsValid(PS->GetActiveHotbar()) ? TEXT("ok") : TEXT("NULL"));
	}

	void ForceCharacterOnline(AFGCharacterPlayer* Char, const TCHAR* Reason)
	{
		if (!IsValid(Char)) return;

		const bool bWasOnline = GetCharacterOnline(Char);

		auto SetOnlineStateFn = GetPrivateMember(FFGCharacterPlayerSetOnlineStateTag{});
		(Char->*SetOnlineStateFn)(true);

		// Direct fallback for the specific broken join where the closed-source
		// function refuses to flip the optional despite valid possession.
		if (!GetCharacterOnline(Char))
		{
			auto IsOnlineMember = GetPrivateMember(FFGCharacterPlayerIsOnlineTag{});
			Char->*IsOnlineMember = true;
		}

		const bool bNowOnline = GetCharacterOnline(Char);
		if (!bWasOnline || !bNowOnline)
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 13: forced character online for %s via %s (was=%d now=%d)"),
				*Char->GetName(), Reason, (int32)bWasOnline, (int32)bNowOnline);
		}
	}

	void ForceActivePlayerState(AFGPlayerController* PC, const TCHAR* Reason)
	{
		if (!IsValid(PC)) return;
		AFGCharacterPlayer* Char = GetFGCharacterFromController(PC);
		if (!IsValid(Char)) return;

		ForceCharacterOnline(Char, Reason);
		RepairPlayerStateHotbars(Cast<AFGPlayerState>(PC->PlayerState), Reason);

		if (!PC->IsRespawning())
		{
			PC->ResetIgnoreInputFlags();

			Char->UpdateMovementModeOnRespawn(false);
			if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
			{
				CMC->SetComponentTickEnabled(true);
				CMC->Activate(true);
				if (CMC->MovementMode == MOVE_None)
				{
					CMC->SetMovementMode(MOVE_Walking);
				}
			}
		}

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 13: active-player repair via %s on PC=%s pawn=%s PS=%s IsRespawning=%d online=%d gate[Jump=%d Hotbar=%d BuildGun=%d]"),
			Reason,
			*PC->GetName(),
			*Char->GetName(),
			IsValid(PC->PlayerState) ? *PC->PlayerState->GetName() : TEXT("<null>"),
			(int32)PC->IsRespawning(),
			(int32)GetCharacterOnline(Char),
			(int32)PC->GetDisabledInputGate().mJump,
			(int32)PC->GetDisabledInputGate().mHotbar,
				(int32)PC->GetDisabledInputGate().mBuildGun);

		// Re-apply current onboarding step to drive the objective panel UI.
		// On heavy-mod saves, mCurrentOnboardingStep often replicates BEFORE
		// AFGPlayerController has bound its OnRep_CurrentOnboardingStep
		// listener via ListenForOnOnboardingStepUpdated() — so the OnRep
		// edge fires into a not-yet-bound delegate and the UI never gets
		// the broadcast. The Replicated property is correct on the client,
		// but the UI was never told. Manually invoking
		// Native_OnOnboardingStepUpdated bypasses the missed-edge problem.
		// Client-only (server has no UI), once per PC.
		if (UWorld* World = PC->GetWorld())
		{
			if (World->GetNetMode() != NM_DedicatedServer)
			{
				static TSet<TObjectKey<AFGPlayerController>> GObjectiveReappliedPCs;
				const TObjectKey<AFGPlayerController> Key(PC);
				if (!GObjectiveReappliedPCs.Contains(Key))
				{
					if (AFGTutorialIntroManager* TutorialIntro = AFGTutorialIntroManager::Get(World))
					{
						UFGOnboardingStep* CurrentStep = TutorialIntro->GetCurrentOnboardingStep();
						auto NativeOnOnboardingStepUpdatedFn = GetPrivateMember(FFGPlayerControllerNativeOnOnboardingStepUpdatedTag{});
						(PC->*NativeOnOnboardingStepUpdatedFn)(CurrentStep);
						GObjectiveReappliedPCs.Add(Key);
						UE_LOG(LogSinkSubsystemRaceFix, Display,
							TEXT("Hook 13: re-applied onboarding step '%s' on %s (objective panel update via %s)"),
							IsValid(CurrentStep) ? *CurrentStep->GetName() : TEXT("<null>"),
							*PC->GetName(), Reason);
					}
				}
			}
		}
	}

	// =================================================================
	// v1.5.0 — Hook 24: chunked lightweight-buildable replication path
	// =================================================================
	// On heavy-mod / large saves the vanilla bulk-send path stalls and
	// the joining client never gets visible meshes for foundations,
	// walls, belts, etc. Vanilla DOES have a chunked-send proxy
	// (AFGLightweightBuildableRepProxy) but only spawns it when
	// CVarUseLegacyLightweightReplication != false — and that defaults
	// to false in modern SF, so the proxy never exists by default.
	//
	// Phase 1: server-side, on OnPossess for non-local PCs, spawn
	// AFGLightweightBuildableRepProxy with Owner=PC and register it via
	// the public RegisterReplicationProxy. The replicated
	// mLightweightBuildableRepProxy property on PC carries the
	// reference to the client; the client subsystem auto-discovers it
	// via mAllLightweightReplicationProxies on next access. No instance
	// enqueue yet — Phase 2 will add the time-sliced AddConstructed drain.

	// v1.5.1: default flipped 1 -> 0. The legacy AFGLightweightBuildableRepProxy
	// path competes with the modern UFGLightweightBuildableReplicationComponent
	// reliable-messaging pipeline (both reliable, both share the per-connection
	// out-of-order buffer), which made heavy-mod joins WORSE than v1.4.0.
	// Kept as opt-in for experimentation; real chunked-replication fix targeting
	// the modern path is deferred to v1.6.0+ alongside the RepGraph budget hook.
	static int32 GRepProxyEnable = 0;
	static FAutoConsoleVariableRef CVarRepProxyEnable(
		TEXT("r.SinkRaceFix.RepProxy.Enable"),
		GRepProxyEnable,
		TEXT("Force-spawn AFGLightweightBuildableRepProxy server-side per joining client (default 0 in v1.5.1). 1 re-enables the v1.5.0 chunked path (experimental — competes with modern reliable-messaging path)."),
		ECVF_Default);

	static int32 GRepProxyEnqueueChunk = 1000;
	static FAutoConsoleVariableRef CVarRepProxyEnqueueChunk(
		TEXT("r.SinkRaceFix.RepProxy.EnqueueChunk"),
		GRepProxyEnqueueChunk,
		TEXT("Max lightweight-buildable instances enqueued into RepProxy per server tick (default 1000, clamped 100..5000)."),
		ECVF_Default);

	static float GRepProxyEnqueueInterval = 0.05f;
	static FAutoConsoleVariableRef CVarRepProxyEnqueueInterval(
		TEXT("r.SinkRaceFix.RepProxy.EnqueueInterval"),
		GRepProxyEnqueueInterval,
		TEXT("Seconds between enqueue ticks (default 0.05 = 20 ticks/sec)."),
		ECVF_Default);

	static TSet<TObjectKey<AFGPlayerController>> GProxySpawnedForPC;

	struct FRepProxyEnqueueCursor
	{
		TWeakObjectPtr<AFGPlayerController> PC;
		TWeakObjectPtr<AFGLightweightBuildableRepProxy> Proxy;
		TArray<TSubclassOf<AFGBuildable>> ClassOrder;
		int32 ClassIndex = 0;
		int32 InstanceIndex = 0;
		int32 TotalEnqueued = 0;
		int32 SkippedEmpty = 0;
		double StartTime = 0.0;
		FTSTicker::FDelegateHandle TickerHandle;
	};

	static TArray<TSharedPtr<FRepProxyEnqueueCursor>> GActiveCursors;

	void DetachCursor(const FRepProxyEnqueueCursor* Raw)
	{
		GActiveCursors.RemoveAll([Raw](const TSharedPtr<FRepProxyEnqueueCursor>& C)
		{
			return !C.IsValid() || C.Get() == Raw;
		});
	}

	bool TickEnqueueCursor(TSharedPtr<FRepProxyEnqueueCursor> Cursor)
	{
		if (!Cursor.IsValid()) return false;
		AFGPlayerController* PC = Cursor->PC.Get();
		AFGLightweightBuildableRepProxy* Proxy = Cursor->Proxy.Get();
		if (!IsValid(PC) || !IsValid(Proxy))
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 24: enqueue cursor abandoned (PC or Proxy gone) — enqueued %d before stop"),
				Cursor->TotalEnqueued);
			DetachCursor(Cursor.Get());
			return false;
		}

		UWorld* World = PC->GetWorld();
		if (!IsValid(World)) { DetachCursor(Cursor.Get()); return false; }
		AFGLightweightBuildableSubsystem* Sub = AFGLightweightBuildableSubsystem::Get(World);
		if (!IsValid(Sub)) { DetachCursor(Cursor.Get()); return false; }

		const auto& AllInstances = Sub->GetAllLightweightBuildableInstances();

		if (Cursor->ClassOrder.Num() == 0)
		{
			AllInstances.GenerateKeyArray(Cursor->ClassOrder);
			if (Cursor->ClassOrder.Num() == 0)
			{
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 24: no lightweight instances to enqueue for PC %s"),
					*PC->GetName());
				DetachCursor(Cursor.Get());
				return false;
			}
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 24: enqueue starting for PC %s — %d buildable classes"),
				*PC->GetName(), Cursor->ClassOrder.Num());
		}

		const int32 Budget = FMath::Clamp(GRepProxyEnqueueChunk, 100, 5000);
		int32 EnqueuedThisTick = 0;

		while (EnqueuedThisTick < Budget && Cursor->ClassIndex < Cursor->ClassOrder.Num())
		{
			const TSubclassOf<AFGBuildable> Cls = Cursor->ClassOrder[Cursor->ClassIndex];
			const TArray<FRuntimeBuildableInstanceData>* Arr = AllInstances.Find(Cls);
			if (!Arr || Cursor->InstanceIndex >= Arr->Num())
			{
				Cursor->ClassIndex++;
				Cursor->InstanceIndex = 0;
				continue;
			}

			const int32 Idx = Cursor->InstanceIndex++;
			FRuntimeBuildableInstanceData* DataPtr = Sub->GetRuntimeDataForBuildableClassAndIndex(Cls, Idx);
			if (!DataPtr)
			{
				Cursor->SkippedEmpty++;
				continue;
			}

			// constructId=0xffff + instigator=nullptr + blueprintBuildIndex=INDEX_NONE
			// → mesh-only delivery, no build VFX (per Replicationoverhaul.md).
			Proxy->AddConstructedRuntimeDataForIndex(Cls, *DataPtr, Idx, 0xffff, nullptr, INDEX_NONE);
			EnqueuedThisTick++;
			Cursor->TotalEnqueued++;
		}

		if (Cursor->ClassIndex >= Cursor->ClassOrder.Num())
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 24: enqueue complete for PC %s — %d instances enqueued (%d empty slots skipped) over %.2fs"),
				*PC->GetName(),
				Cursor->TotalEnqueued,
				Cursor->SkippedEmpty,
				FPlatformTime::Seconds() - Cursor->StartTime);
			DetachCursor(Cursor.Get());
			return false;
		}

		return true;
	}

	void StartEnqueueDrainForPC(AFGPlayerController* PC, AFGLightweightBuildableRepProxy* Proxy)
	{
		if (!IsValid(PC) || !IsValid(Proxy)) return;

		// Drop any stale cursor for the same PC (reconnect, repossession edge cases).
		GActiveCursors.RemoveAll([PC](const TSharedPtr<FRepProxyEnqueueCursor>& C)
		{
			if (!C.IsValid()) return true;
			if (C->PC.Get() == PC)
			{
				if (C->TickerHandle.IsValid())
				{
					FTSTicker::GetCoreTicker().RemoveTicker(C->TickerHandle);
				}
				return true;
			}
			return false;
		});

		auto Cursor = MakeShared<FRepProxyEnqueueCursor>();
		Cursor->PC = PC;
		Cursor->Proxy = Proxy;
		Cursor->StartTime = FPlatformTime::Seconds();

		const float Interval = FMath::Clamp(GRepProxyEnqueueInterval, 0.0f, 1.0f);
		Cursor->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([Cursor](float /*Dt*/)
			{
				return TickEnqueueCursor(Cursor);
			}), Interval);

		GActiveCursors.Add(Cursor);
	}

	void EnsureLightweightRepProxyForPC(AFGPlayerController* PC, const TCHAR* Reason)
	{
		if (!IsValid(PC)) return;
		if (GRepProxyEnable == 0) return;

		UWorld* World = PC->GetWorld();
		if (!IsValid(World)) return;

		const ENetMode NM = World->GetNetMode();
		if (NM != NM_DedicatedServer && NM != NM_ListenServer) return;
		if (!PC->HasAuthority()) return;
		// Listen-server host's own PC has the data locally; vanilla skips it
		// for the same reason. Spawning a proxy for a same-process owner just
		// burns memory.
		if (PC->IsLocalController()) return;

		const TObjectKey<AFGPlayerController> Key(PC);
		if (GProxySpawnedForPC.Contains(Key)) return;

		AFGLightweightBuildableSubsystem* Sub = AFGLightweightBuildableSubsystem::Get(World);
		if (!IsValid(Sub)) return;

		// If vanilla legacy-replication path or another mod already spawned
		// a proxy on this PC, register it with the subsystem (vanilla doesn't
		// always do this), kick off the enqueue drain, and skip our spawn.
		if (IsValid(PC->mLightweightBuildableRepProxy))
		{
			Sub->RegisterReplicationProxy(PC->mLightweightBuildableRepProxy);
			GProxySpawnedForPC.Add(Key);
			StartEnqueueDrainForPC(PC, PC->mLightweightBuildableRepProxy);
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 24: PC %s already had RepProxy %s via %s — registered + drain started"),
				*PC->GetName(),
				*PC->mLightweightBuildableRepProxy->GetName(),
				Reason);
			return;
		}

		FActorSpawnParameters Params;
		Params.Owner = PC;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AFGLightweightBuildableRepProxy* Proxy = World->SpawnActor<AFGLightweightBuildableRepProxy>(
			AFGLightweightBuildableRepProxy::StaticClass(),
			FTransform::Identity,
			Params);
		if (!IsValid(Proxy))
		{
			UE_LOG(LogSinkSubsystemRaceFix, Warning,
				TEXT("Hook 24: SpawnActor<AFGLightweightBuildableRepProxy> returned null for PC %s via %s"),
				*PC->GetName(), Reason);
			return;
		}

		FAttachmentTransformRules AttachRules(EAttachmentRule::SnapToTarget,
			EAttachmentRule::SnapToTarget, EAttachmentRule::SnapToTarget, true);
		Proxy->AttachToActor(PC, AttachRules);

		PC->mLightweightBuildableRepProxy = Proxy;
		PC->ForceNetUpdate();

		Sub->RegisterReplicationProxy(Proxy);
		GProxySpawnedForPC.Add(Key);

		StartEnqueueDrainForPC(PC, Proxy);

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 24: spawned AFGLightweightBuildableRepProxy %s for PC %s via %s (NM=%s) — drain started"),
			*Proxy->GetName(),
			*PC->GetName(),
			Reason,
			NetModeName(NM));
	}

	void InstallRepProxyClientWiringHook()
	{
		// Phase 3: when a replicated AFGLightweightBuildableRepProxy actor's
		// BeginPlay fires on a remote (client) end, register it with the local
		// subsystem and call NotifyRepProxyCreated so mCachedLocalRepProxy is
		// set. Vanilla never calls NotifyRepProxyCreated from anywhere we can
		// find in the decompile — it relies on a fallback walk of
		// mAllLightweightReplicationProxies, which is per-process and empty on
		// the client unless we register it ourselves. RegisterReplicationProxy
		// is AddUnique and NotifyRepProxyCreated is an idempotent setter, so
		// firing on both server and client is safe.
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableRepProxy, BeginPlay,
			[](AFGLightweightBuildableRepProxy* Self)
			{
				if (!IsValid(Self)) return;
				UWorld* World = Self->GetWorld();
				if (!IsValid(World)) return;
				AFGLightweightBuildableSubsystem* Sub = AFGLightweightBuildableSubsystem::Get(World);
				if (!IsValid(Sub)) return;

				Sub->RegisterReplicationProxy(Self);
				Sub->NotifyRepProxyCreated(Self);

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 25: RepProxy %s BeginPlay → registered + cached on subsystem (NM=%s)"),
					*Self->GetName(),
					NetModeName(World->GetNetMode()));
			});

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 25: RepProxy client-wiring hook installed"));
	}

	int32 CountPendingLightweightGameStateInstances(AFGLightweightBuildableSubsystem* Sub)
	{
		if (!IsValid(Sub)) return 0;

		auto PendingMember = GetPrivateMember(FFGLightweightPendingGameStateInstancesTag{});
		int32 PendingCount = 0;
		for (const TPair<TSubclassOf<AFGBuildable>, TArray<FPendingGamestateRuntimeDataAdd>>& Pair : Sub->*PendingMember)
		{
			PendingCount += Pair.Value.Num();
		}
		return PendingCount;
	}

	bool LightweightRuntimeDataLooksResolved(AFGLightweightBuildableSubsystem* Sub, TSubclassOf<AFGBuildable> BuildableClass, int32 Index)
	{
		if (!IsValid(Sub)) return false;
		if (BuildableClass == nullptr) return false;
		if (Index == INDEX_NONE) return false;

		FRuntimeBuildableInstanceData* Data = Sub->GetRuntimeDataForBuildableClassAndIndex(BuildableClass, Index);
		return Data != nullptr && Data->IsValid();
	}

	FString MakeTopClassCountsString(const TMap<FString, int32>& Counts, int32 Limit)
	{
		TArray<TPair<FString, int32>> Pairs;
		Pairs.Reserve(Counts.Num());
		for (const TPair<FString, int32>& Pair : Counts)
		{
			Pairs.Emplace(Pair.Key, Pair.Value);
		}

		Pairs.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
		{
			return A.Value > B.Value;
		});

		FString Result;
		const int32 NumToPrint = FMath::Min(Limit, Pairs.Num());
		for (int32 Index = 0; Index < NumToPrint; ++Index)
		{
			if (!Result.IsEmpty())
			{
				Result += TEXT(", ");
			}
			Result += FString::Printf(TEXT("%s=%d"), *Pairs[Index].Key, Pairs[Index].Value);
		}
		return Result.IsEmpty() ? TEXT("none") : Result;
	}

	FString BuildBuildableVisibilityCensus(UWorld* World)
	{
		if (!IsValid(World)) return TEXT("World=NULL");

		TMap<FString, int32> BuildableClassCounts;
		int32 BuildableCount = 0;
		if (AFGBuildableSubsystem* BuildableSub = AFGBuildableSubsystem::Get(World))
		{
			const TArray<AFGBuildable*>& Buildables = BuildableSub->GetAllBuildablesRef();
			BuildableCount = Buildables.Num();
			for (AFGBuildable* Buildable : Buildables)
			{
				if (!IsValid(Buildable) || Buildable->GetClass() == nullptr) continue;
				BuildableClassCounts.FindOrAdd(Buildable->GetClass()->GetName())++;
			}
		}

		int32 LightweightClasses = 0;
		int32 LightweightRuntime = 0;
		int32 LightweightResolved = 0;
		int32 LightweightHandles = 0;
		TMap<FString, int32> LightweightClassCounts;
		if (AFGLightweightBuildableSubsystem* LightweightSub = AFGLightweightBuildableSubsystem::Get(World))
		{
			for (const TPair<TSubclassOf<AFGBuildable>, TArray<FRuntimeBuildableInstanceData>>& Pair : LightweightSub->GetAllLightweightBuildableInstances())
			{
				++LightweightClasses;
				const FString ClassName = Pair.Key != nullptr ? Pair.Key->GetName() : TEXT("NULL");
				LightweightClassCounts.FindOrAdd(ClassName) += Pair.Value.Num();
				LightweightRuntime += Pair.Value.Num();
				for (const FRuntimeBuildableInstanceData& Data : Pair.Value)
				{
					if (Data.IsValid())
					{
						++LightweightResolved;
					}
					LightweightHandles += Data.Handles.Num();
				}
			}
		}

		int32 AbstractEntries = 0;
		int32 AbstractMeshComponents = 0;
		int32 AbstractCollisionComponents = 0;
		int32 AbstractHandles = 0;
		if (AAbstractInstanceManager* InstanceManager = AAbstractInstanceManager::GetInstanceManager(World))
		{
			auto InstanceMapMember = GetPrivateMember(FFGAbstractInstanceManagerInstanceMapTag{});
			for (const TPair<FName, FInstanceComponentData>& Pair : InstanceManager->*InstanceMapMember)
			{
				++AbstractEntries;
				AbstractMeshComponents += Pair.Value.InstancedStaticMeshComponents.Num();
				AbstractCollisionComponents += Pair.Value.InstancedCollisionComponents.Num();
				for (const TPair<int32, TArray<FInstanceOwnerHandlePtr>>& HandlesByMeshComponent : Pair.Value.InstanceHandles)
				{
					AbstractHandles += HandlesByMeshComponent.Value.Num();
				}
			}
		}

		int32 LightweightInitialComponents = 0;
		int32 LightweightInitialReceived = 0;
		for (TActorIterator<AFGPlayerController> It(World); It; ++It)
		{
			AFGPlayerController* PC = *It;
			if (!IsValid(PC)) continue;
			if (UFGLightweightBuildableReplicationComponent* RepComp = PC->FindComponentByClass<UFGLightweightBuildableReplicationComponent>())
			{
				++LightweightInitialComponents;
				if (RepComp->HasReceivedInitialReplicationData())
				{
					++LightweightInitialReceived;
				}
			}
		}

		return FString::Printf(
			TEXT("buildables=%d topBuildables=[%s] lightweightClasses=%d lightweightRuntime=%d lightweightResolved=%d lightweightHandles=%d topLightweight=[%s] abstractEntries=%d abstractMeshComps=%d abstractCollisionComps=%d abstractHandles=%d lightRepInitial=%d/%d"),
			BuildableCount,
			*MakeTopClassCountsString(BuildableClassCounts, 8),
			LightweightClasses,
			LightweightRuntime,
			LightweightResolved,
			LightweightHandles,
			*MakeTopClassCountsString(LightweightClassCounts, 8),
			AbstractEntries,
			AbstractMeshComponents,
			AbstractCollisionComponents,
			AbstractHandles,
			LightweightInitialReceived,
			LightweightInitialComponents);
	}

	void ScheduleLightweightGamestateNotifyRetry(AFGLightweightBuildableSubsystem* Sub, const TCHAR* Reason)
	{
		if (!IsValid(Sub)) return;
		UWorld* World = Sub->GetWorld();
		if (!IsValid(World) || World->GetNetMode() != NM_Client) return;

		static TSet<FObjectKey> ActiveRetries;
		const FObjectKey Key(Sub);
		if (ActiveRetries.Contains(Key)) return;
		ActiveRetries.Add(Key);

		static constexpr int32 MAX_ATTEMPTS = 80;      // 80 * 0.25s = 20s
		static constexpr float RETRY_INTERVAL = 0.25f;
		TWeakObjectPtr<AFGLightweightBuildableSubsystem> WeakSub(Sub);
		const FObjectKey RetryKey(Sub);
		TSharedRef<int32, ESPMode::ThreadSafe> Attempts = MakeShared<int32, ESPMode::ThreadSafe>(0);

		UE_LOG(LogSinkSubsystemRaceFix, Verbose,
			TEXT("Hook 27: scheduling lightweight NotifyGamestateReceived retry (%s), pending=%d"),
			Reason,
			CountPendingLightweightGameStateInstances(Sub));

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakSub, RetryKey, Attempts](float) mutable
			{
				AFGLightweightBuildableSubsystem* RetrySub = WeakSub.Get();
				if (!IsValid(RetrySub))
				{
					ActiveRetries.Remove(RetryKey);
					return false;
				}

				UWorld* RetryWorld = RetrySub->GetWorld();
				if (!IsValid(RetryWorld) || RetryWorld->GetNetMode() != NM_Client)
				{
					ActiveRetries.Remove(RetryKey);
					return false;
				}

				++(*Attempts);
				const int32 PendingBefore = CountPendingLightweightGameStateInstances(RetrySub);
				RetrySub->NotifyGamestateReceived();
				const int32 PendingAfter = CountPendingLightweightGameStateInstances(RetrySub);

				if (PendingBefore != PendingAfter || *Attempts == 1)
				{
					UE_LOG(LogSinkSubsystemRaceFix, Verbose,
						TEXT("Hook 27: NotifyGamestateReceived retry attempt %d pending %d -> %d"),
						*Attempts,
						PendingBefore,
						PendingAfter);
				}

				if (PendingAfter == 0 || *Attempts >= MAX_ATTEMPTS)
				{
					ActiveRetries.Remove(RetryKey);
					if (PendingAfter != 0)
					{
						UE_LOG(LogSinkSubsystemRaceFix, Warning,
							TEXT("Hook 27: lightweight pending-gamestate retry expired after %d attempts, pending=%d"),
							*Attempts,
							PendingAfter);
					}
					return false;
				}

				return true;
			}),
			RETRY_INTERVAL);
	}

	void InstallLightweightBuildableResolveRepairHook()
	{
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, NotifyGamestateReceived,
			[](AFGLightweightBuildableSubsystem* Self)
			{
				if (!IsValid(Self)) return;
				UWorld* World = Self->GetWorld();
				if (!IsValid(World) || World->GetNetMode() != NM_Client) return;

				const int32 Pending = CountPendingLightweightGameStateInstances(Self);
				const auto& AllInstances = Self->GetAllLightweightBuildableInstances();

				if (!LogSinkSubsystemRaceFix.IsSuppressed(ELogVerbosity::Verbose))
				{
					int32 ClassCount = 0;
					int32 RuntimeCount = 0;
					int32 ResolvedCount = 0;
					for (const TPair<TSubclassOf<AFGBuildable>, TArray<FRuntimeBuildableInstanceData>>& Pair : AllInstances)
					{
						++ClassCount;
						RuntimeCount += Pair.Value.Num();
						for (const FRuntimeBuildableInstanceData& Data : Pair.Value)
						{
							if (Data.IsValid())
							{
								++ResolvedCount;
							}
						}
					}

					UE_LOG(LogSinkSubsystemRaceFix, Verbose,
						TEXT("Hook 27: NotifyGamestateReceived observed client lightweight state classes=%d runtime=%d resolved=%d pending=%d"),
						ClassCount,
						RuntimeCount,
						ResolvedCount,
						Pending);
				}
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGLightweightBuildableSubsystem, AddFromReplicatedData,
			[](AFGLightweightBuildableSubsystem* Self,
				TSubclassOf<AFGBuildable> BuildableClass,
				TSubclassOf<UFGRecipe> /*BuiltWithRecipe*/,
				const FLightweightBuildableReplicationItem& ReplicationData,
				int32 /*MaxSize*/,
				AActor* /*BuildEffectInstigator*/,
				int32 /*BlueprintBuildIndex*/)
			{
				if (!IsValid(Self)) return;
				UWorld* World = Self->GetWorld();
				if (!IsValid(World) || World->GetNetMode() != NM_Client) return;
				if (BuildableClass == nullptr || ReplicationData.Index == INDEX_NONE) return;

				if (!LogSinkSubsystemRaceFix.IsSuppressed(ELogVerbosity::Verbose))
				{
					const int32 PendingNow = CountPendingLightweightGameStateInstances(Self);
					const bool bResolvedNow = LightweightRuntimeDataLooksResolved(Self, BuildableClass, ReplicationData.Index);
					UE_LOG(LogSinkSubsystemRaceFix, Verbose,
						TEXT("Hook 27: lightweight replicated add observed class=%s index=%d resolved=%d pending=%d"),
						*BuildableClass->GetPathName(),
						ReplicationData.Index,
						(int32)bResolvedNow,
						PendingNow);
				}

				if (LightweightRuntimeDataLooksResolved(Self, BuildableClass, ReplicationData.Index))
				{
					return;
				}

				if (!LogSinkSubsystemRaceFix.IsSuppressed(ELogVerbosity::Verbose))
				{
					FRuntimeBuildableInstanceData* Data = Self->GetRuntimeDataForBuildableClassAndIndex(BuildableClass, ReplicationData.Index);
					UE_LOG(LogSinkSubsystemRaceFix, Verbose,
						TEXT("Hook 27: lightweight replicated add did not resolve handles yet class=%s index=%d repValid=%d data=%d handles=%d pending=%d"),
						*BuildableClass->GetPathName(),
						ReplicationData.Index,
						(int32)ReplicationData.IsValid,
						(int32)(Data != nullptr),
						Data != nullptr ? Data->Handles.Num() : -1,
						CountPendingLightweightGameStateInstances(Self));
				}

				ScheduleLightweightGamestateNotifyRetry(Self, TEXT("AddFromReplicatedData unresolved handles"));
			});

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 27 installed: lightweight buildable pending-gamestate flush + unresolved replicated-add diagnostics"));
	}

	static FTSTicker::FDelegateHandle GBuildableVisibilityCensusHandle;

	static int32 GCensusEnable = 0;
	static FAutoConsoleVariableRef CVarCensusEnable(
		TEXT("r.SinkRaceFix.Census.Enable"),
		GCensusEnable,
		TEXT("Hook 28 buildable/lightweight/abstract-instance visibility census. 0 = off (default), 1 = log every 5s for ~120s after install."),
		ECVF_Default);

	bool BuildableVisibilityCensusTick(float /*Delta*/)
	{
		if (!GEngine) return true;

		static int32 RemainingSamples = 24; // 24 * 5s = two minutes after module load / join.
		if (RemainingSamples <= 0) return false;

		bool bLoggedAnyWorld = false;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* World = Ctx.World();
			if (!IsValid(World)) continue;
			if (Ctx.WorldType != EWorldType::Game) continue;
			if (World->GetNetMode() == NM_Standalone) continue;

			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 28: buildable visibility census mode=%s t=%.1f %s"),
				NetModeName(World->GetNetMode()),
				World->GetTimeSeconds(),
				*BuildBuildableVisibilityCensus(World));
			bLoggedAnyWorld = true;
		}

		if (bLoggedAnyWorld)
		{
			--RemainingSamples;
		}

		return RemainingSamples > 0;
	}

	void InstallBuildableVisibilityCensusHook()
	{
		if (GCensusEnable == 0)
		{
			UE_LOG(LogSinkSubsystemRaceFix, Verbose,
				TEXT("Hook 28: skipped — r.SinkRaceFix.Census.Enable=0 (default). Set 1 to enable diagnostic census."));
			return;
		}

		if (!GBuildableVisibilityCensusHandle.IsValid())
		{
			GBuildableVisibilityCensusHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&BuildableVisibilityCensusTick),
				5.0f);
		}

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 28 installed: buildable/lightweight/abstract-instance visibility census for first 120s"));
	}

	// Hook 29: the RepGraph actor-discovery budget is only one side of the
	// bottleneck. The actual connection can still be capped by the client's
	// ConfiguredInternetSpeed / NetDriver MaxClientRate. In the failing log the
	// server accepted the client at 120000 bytes/sec while Hook 26 had raised
	// actor discovery to 500 kBps, so actor replication still starved. Force the
	// per-player net speed from both ends during the join window.
	static int32 GForcedJoinNetSpeed = 500000;
	static FAutoConsoleVariableRef CVarForcedJoinNetSpeed(
		TEXT("r.SinkRaceFix.NetSpeed.ForcedJoinNetSpeed"),
		GForcedJoinNetSpeed,
		TEXT("NetSpeed applied to player controllers during join to avoid stale 120000-byte/sec client config caps. Default 500000. Set 0 to disable."),
		ECVF_Default);

	static FTSTicker::FDelegateHandle GForcedNetSpeedHandle;

	void ApplyForcedJoinNetSpeed(APlayerController* PC, const TCHAR* Reason)
	{
		if (!IsValid(PC)) return;
		if (GForcedJoinNetSpeed <= 0) return;

		UWorld* World = PC->GetWorld();
		if (!IsValid(World) || World->GetNetMode() == NM_Standalone) return;

		const int32 NetSpeed = FMath::Clamp(GForcedJoinNetSpeed, 20000, 2000000);
		PC->SetNetSpeed(NetSpeed);

		static TSet<FString> LoggedKeys;
		const FString Key = FString::Printf(TEXT("%s:%s:%s"), NetModeName(World->GetNetMode()), *PC->GetName(), Reason);
		if (!LoggedKeys.Contains(Key))
		{
			LoggedKeys.Add(Key);
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 29: forced NetSpeed=%d on %s (%s, mode=%s)"),
				NetSpeed,
				*PC->GetName(),
				Reason,
				NetModeName(World->GetNetMode()));
		}
	}

	bool ForcedJoinNetSpeedTick(float /*Delta*/)
	{
		if (!GEngine) return true;

		static int32 RemainingSamples = 60; // 60 * 2s = two minutes.
		if (RemainingSamples <= 0) return false;

		bool bSawNetworkWorld = false;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* World = Ctx.World();
			if (!IsValid(World)) continue;
			if (Ctx.WorldType != EWorldType::Game) continue;
			if (World->GetNetMode() == NM_Standalone) continue;

			bSawNetworkWorld = true;
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				ApplyForcedJoinNetSpeed(It->Get(), TEXT("ticker"));
			}
		}

		if (bSawNetworkWorld)
		{
			--RemainingSamples;
		}
		return RemainingSamples > 0;
	}

	void InstallForcedJoinNetSpeedHook()
	{
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerController, BeginPlay,
			[](AFGPlayerController* Self)
			{
				ApplyForcedJoinNetSpeed(Self, TEXT("BeginPlayAfter"));
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerController, NotifyLoadedWorld,
			[](AFGPlayerController* Self, FName /*WorldPackageName*/, bool /*bIsFinalDest*/)
			{
				ApplyForcedJoinNetSpeed(Self, TEXT("NotifyLoadedWorldAfter"));
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerController, OnPossess,
			[](AFGPlayerController* Self, APawn* /*Pawn*/)
			{
				ApplyForcedJoinNetSpeed(Self, TEXT("OnPossessAfter"));
			});

		if (!GForcedNetSpeedHandle.IsValid())
		{
			GForcedNetSpeedHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&ForcedJoinNetSpeedTick),
				2.0f);
		}

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 29 installed: forced join NetSpeed default=%d via r.SinkRaceFix.NetSpeed.ForcedJoinNetSpeed"),
			GForcedJoinNetSpeed);
	}

	void InstallJoinCompletionHook()
	{
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerController, NotifyLoadedWorld,
			[](AFGPlayerController* Self, FName WorldPackageName, bool bIsFinalDest)
			{
				if (!IsValid(Self)) return;
				bool bRecipeInitial = false;
				bool bSchematicInitial = false;
				GetInitialManagerReplicationReady(Self, bRecipeInitial, bSchematicInitial);
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 13: NotifyLoadedWorld after vanilla (%s final=%d) on %s ready=%d respawning=%d recipeInitial=%d schematicInitial=%d"),
					*WorldPackageName.ToString(),
					(int32)bIsFinalDest,
					*Self->GetName(),
					(int32)AreJoinCriticalObjectsReady(Self),
					(int32)Self->IsRespawning(),
					(int32)bRecipeInitial,
					(int32)bSchematicInitial);

				if (bIsFinalDest)
				{
					ForceActivePlayerState(Self, TEXT("NotifyLoadedWorldAfter"));
				}
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerController, OnPossess,
			[](AFGPlayerController* Self, APawn* /*Pawn*/)
			{
				ForceActivePlayerState(Self, TEXT("OnPossessAfter"));
				EnsureLightweightRepProxyForPC(Self, TEXT("OnPossessAfter"));
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerController, OnRep_PlayerState,
			[](AFGPlayerController* Self)
			{
				ForceActivePlayerState(Self, TEXT("PC_OnRep_PlayerStateAfter"));
			});

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 13/24: join/respawn completion repair + lightweight RepProxy spawn hooks installed"));
	}

	static FTSTicker::FDelegateHandle GManagerReplicationDiagHandle;
	static FTSTicker::FDelegateHandle GGatedRespawnFinishHandle;

	// Hook 19 was removed in v1.4.0 — it called NotifyGamestateReceived() on
	// AFGLightweightBuildableSubsystem when HasReceivedInitialLightweightReplicationData()
	// first flipped true on the client, hoping to drain a pending-instance
	// buffer that the natural flush had missed. Per Ghidra decompilation of
	// AFGGameState::BeginPlay (line 899-900), SF *already* calls
	// NotifyGamestateReceived itself; with Hook 23 ensuring mod init runs after
	// AFGGameState::BeginPlay has executed, our re-fire was provably redundant.

	bool ManagerReplicationDiagTick(float /*Delta*/)
	{
		if (!GEngine) return true;

		static TMap<FObjectKey, FString> LastStateByController;
		static int32 LogBudget = 120;

		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* World = Ctx.World();
			if (!IsValid(World)) continue;
			if (Ctx.WorldType != EWorldType::Game) continue;

			for (TActorIterator<AFGPlayerController> It(World); It; ++It)
			{
				AFGPlayerController* PC = *It;
				if (!IsValid(PC)) continue;

				if (World->GetNetMode() == NM_Client)
				{
					auto HasInitialLightweightDataFn = GetPrivateMember(FFGPlayerControllerHasInitialLightweightDataTag{});
					if ((PC->*HasInitialLightweightDataFn)())
					{
						if (AFGLightweightBuildableSubsystem* LightweightSub = AFGLightweightBuildableSubsystem::Get(World))
						{
							if (CountPendingLightweightGameStateInstances(LightweightSub) > 0)
							{
								ScheduleLightweightGamestateNotifyRetry(LightweightSub, TEXT("PC initial lightweight data ready"));
							}
						}
					}
				}

				const FObjectKey Key(PC);
				const FString State = BuildManagerReplicationState(PC);
				FString* LastState = LastStateByController.Find(Key);
				if (LastState != nullptr && *LastState == State) continue;

				LastStateByController.Add(Key, State);
				if (LogBudget > 0)
				{
					--LogBudget;
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Hook 16: manager replication state changed: %s"),
						*State);
				}
			}
		}

		return true;
	}

	bool GatedRespawnFinishTick(float /*Delta*/)
	{
		if (!GEngine) return true;

		static TMap<FObjectKey, double> ReadySinceByController;
		static TSet<FObjectKey> FinishedControllers;
		static const double READY_STABLE_SECONDS = 3.0;

		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			UWorld* World = Ctx.World();
			if (!IsValid(World)) continue;
			if (Ctx.WorldType != EWorldType::Game) continue;
			if (World->GetNetMode() == NM_DedicatedServer) continue;

			for (TActorIterator<AFGPlayerController> It(World); It; ++It)
			{
				AFGPlayerController* PC = *It;
				if (!IsValid(PC)) continue;
				if (!PC->IsRespawning()) continue;

				const FObjectKey Key(PC);
				if (FinishedControllers.Contains(Key)) continue;

				if (!AreRespawnCompletionPredicatesReady(PC))
				{
					ReadySinceByController.Remove(Key);
					continue;
				}

				const double Now = FPlatformTime::Seconds();
				double* ReadySince = ReadySinceByController.Find(Key);
				if (ReadySince == nullptr)
				{
					ReadySinceByController.Add(Key, Now);
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Hook 17: respawn completion predicates ready; waiting %.1fs before finish: %s"),
						READY_STABLE_SECONDS,
						*BuildManagerReplicationState(PC));
					continue;
				}

				if ((Now - *ReadySince) < READY_STABLE_SECONDS) continue;

				FinishedControllers.Add(Key);
				ReadySinceByController.Remove(Key);

				auto ClientDoneRespawningFn = GetPrivateMember(FFGPlayerControllerClientDoneRespawningTag{});
				auto FinishRespawnFn = GetPrivateMember(FFGPlayerControllerFinishRespawnTag{});
				(PC->*ClientDoneRespawningFn)();
				(PC->*FinishRespawnFn)();
				ForceActivePlayerState(PC, TEXT("GatedRespawnFinish"));

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 17: completed stuck respawn after all predicates were stable: %s"),
					*BuildManagerReplicationState(PC));
			}
		}

		return true;
	}

	void InstallManagerReplicationDiagnosticsHook()
	{
		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGSchematicManagerReplicationComponent, BeginPlay,
			[](UFGSchematicManagerReplicationComponent* Self)
			{
				if (!IsValid(Self)) return;
				LogManagerReplicationState(Cast<AFGPlayerController>(Self->GetOwner()), TEXT("schematic component BeginPlay after"));
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGSchematicManagerReplicationComponent, EndPlay,
			[](UFGSchematicManagerReplicationComponent* Self, const EEndPlayReason::Type EndPlayReason)
			{
				if (!IsValid(Self)) return;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 16: schematic component EndPlay reason=%d owner=%s"),
					(int32)EndPlayReason,
					IsValid(Self->GetOwner()) ? *Self->GetOwner()->GetName() : TEXT("NULL"));
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGRecipeManagerReplicationComponent, BeginPlay,
			[](UFGRecipeManagerReplicationComponent* Self)
			{
				if (!IsValid(Self)) return;
				LogManagerReplicationState(Cast<AFGPlayerController>(Self->GetOwner()), TEXT("recipe component BeginPlay after"));
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(UFGRecipeManagerReplicationComponent, EndPlay,
			[](UFGRecipeManagerReplicationComponent* Self, const EEndPlayReason::Type EndPlayReason)
			{
				if (!IsValid(Self)) return;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 16: recipe component EndPlay reason=%d owner=%s"),
					(int32)EndPlayReason,
					IsValid(Self->GetOwner()) ? *Self->GetOwner()->GetName() : TEXT("NULL"));
			});

		if (!GManagerReplicationDiagHandle.IsValid())
		{
			GManagerReplicationDiagHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&ManagerReplicationDiagTick),
				2.0f);
		}
		if (!GGatedRespawnFinishHandle.IsValid())
		{
			GGatedRespawnFinishHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateStatic(&GatedRespawnFinishTick),
				0.5f);
		}

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 16/17/19: manager replication diagnostics, gated respawn finisher, and lightweight-buildable gamestate flush installed"));
	}

	void InstallSubsystemKickHook()
	{
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGPlayerState, BeginPlay,
			[](AFGPlayerState* Self)
			{
				if (!IsValid(Self)) return;
				if (Self->GetLocalRole() != ROLE_Authority) return;
				UWorld* World = Self->GetWorld();
				if (!IsValid(World)) return;

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Starting subsystem replication kick (%d ticks @ %.1fs interval) ")
					TEXT("after PlayerState BeginPlay (%s)"),
					KICK_TICKS, KICK_INTERVAL, *Self->GetName());

				KickSubsystemReplication(TWeakObjectPtr<UWorld>(World), KICK_TICKS);
			});
	}

	static TSet<FObjectKey> GConveyorSegmentHandoffRetries;
	static TSet<FObjectKey> GConveyorItemUpdateRetries;

	AFGConveyorChainSubsystem* GetReadyConveyorChainSubsystem(UWorld* World)
	{
		if (World == nullptr) return nullptr;
		if (World->GetGameState() == nullptr) return nullptr;
		return AFGConveyorChainSubsystem::Get(World);
	}

	void ScheduleConveyorSegmentHandoffRetry(AFGConveyorChainActor* ChainActor, const TCHAR* Reason)
	{
		if (!IsValid(ChainActor)) return;
		if (ChainActor->HasAuthority()) return;
		if (!ChainActor->HasBuildChainSegments()) return;

		const FObjectKey Key(ChainActor);
		if (GConveyorSegmentHandoffRetries.Contains(Key)) return;

		GConveyorSegmentHandoffRetries.Add(Key);

		static const int32 MAX_ATTEMPTS = 100;      // 100 * 0.2s = 20s
		static const float RETRY_INTERVAL = 0.2f;
		TWeakObjectPtr<AFGConveyorChainActor> WeakChain(ChainActor);
		TSharedRef<int32, ESPMode::ThreadSafe> Attempts = MakeShared<int32, ESPMode::ThreadSafe>(0);

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 20: scheduling conveyor segment handoff retry for %s (%s)"),
			*ChainActor->GetName(),
			Reason);

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakChain, Attempts](float) mutable
			{
				AFGConveyorChainActor* Chain = WeakChain.Get();
				if (!IsValid(Chain))
				{
					return false;
				}

				const FObjectKey RetryKey(Chain);
				if (Chain->HasAuthority() || !Chain->HasBuildChainSegments())
				{
					GConveyorSegmentHandoffRetries.Remove(RetryKey);
					return false;
				}

				++(*Attempts);
				UWorld* World = Chain->GetWorld();
				AFGConveyorChainSubsystem* ConveyorChains = GetReadyConveyorChainSubsystem(World);
				if (ConveyorChains != nullptr)
				{
					ConveyorChains->NotifyChainReceiveSegmentUpdate(Chain);
					GConveyorSegmentHandoffRetries.Remove(RetryKey);

					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Hook 20: retried conveyor segment handoff succeeded for %s after %d attempts"),
						*Chain->GetName(),
						*Attempts);
					return false;
				}

				if (*Attempts >= MAX_ATTEMPTS)
				{
					GConveyorSegmentHandoffRetries.Remove(RetryKey);
					UE_LOG(LogSinkSubsystemRaceFix, Warning,
						TEXT("Hook 20: conveyor segment handoff retry expired for %s after %d attempts"),
						*Chain->GetName(),
						*Attempts);
					return false;
				}

				return true;
			}),
			RETRY_INTERVAL);
	}

	void ScheduleConveyorItemUpdateRetry(AFGConveyorChainActor* ChainActor, const TCHAR* Reason)
	{
		if (!IsValid(ChainActor)) return;
		if (ChainActor->HasAuthority()) return;
		if (!ChainActor->HasBuildChainSegments()) return;

		const FObjectKey Key(ChainActor);
		if (GConveyorItemUpdateRetries.Contains(Key)) return;

		GConveyorItemUpdateRetries.Add(Key);

		static const int32 MAX_ATTEMPTS = 150;      // 150 * 0.2s = 30s
		static const float RETRY_INTERVAL = 0.2f;
		TWeakObjectPtr<AFGConveyorChainActor> WeakChain(ChainActor);
		TSharedRef<int32, ESPMode::ThreadSafe> Attempts = MakeShared<int32, ESPMode::ThreadSafe>(0);

		UE_LOG(LogSinkSubsystemRaceFix, Verbose,
			TEXT("Hook 21: scheduling conveyor item update retry for %s (%s)"),
			*ChainActor->GetName(),
			Reason);

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakChain, Attempts](float) mutable
			{
				AFGConveyorChainActor* Chain = WeakChain.Get();
				if (!IsValid(Chain))
				{
					return false;
				}

				const FObjectKey RetryKey(Chain);
				if (Chain->HasAuthority() || !Chain->HasBuildChainSegments())
				{
					GConveyorItemUpdateRetries.Remove(RetryKey);
					return false;
				}

				++(*Attempts);
				AFGConveyorChainSubsystem* ConveyorChains = GetReadyConveyorChainSubsystem(Chain->GetWorld());
				if (ConveyorChains != nullptr && ConveyorChains->GetItemDescriptorLookup().Num() > 0)
				{
					ConveyorChains->NotifyChainNeedsItemUpdate(Chain);
					GConveyorItemUpdateRetries.Remove(RetryKey);

					UE_LOG(LogSinkSubsystemRaceFix, Verbose,
						TEXT("Hook 21: queued conveyor item update for %s after %d attempts"),
						*Chain->GetName(),
						*Attempts);
					return false;
				}

				if (*Attempts >= MAX_ATTEMPTS)
				{
					GConveyorItemUpdateRetries.Remove(RetryKey);
					UE_LOG(LogSinkSubsystemRaceFix, Warning,
						TEXT("Hook 21: conveyor item update retry expired for %s after %d attempts"),
						*Chain->GetName(),
						*Attempts);
					return false;
				}

				return true;
			}),
			RETRY_INTERVAL);
	}

	void InstallConveyorAnimationHooks()
	{
		SUBSCRIBE_UOBJECT_METHOD(AFGConveyorChainSubsystem, IsUnusuallyLargeTickDelta,
			[](auto& Scope, AFGConveyorChainSubsystem* Self)
			{
				if (!IsValid(Self)) return;
				if (Self->HasAuthority()) return;

				Scope.Override(false);
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGConveyorChainActor, NetUpdateBuildSpline,
			[](AFGConveyorChainActor* Self)
			{
				if (!IsValid(Self)) return;
				if (Self->HasAuthority()) return;
				if (!Self->HasBuildChainSegments()) return;

				UWorld* World = Self->GetWorld();
				AFGConveyorChainSubsystem* ConveyorChains = GetReadyConveyorChainSubsystem(World);
				if (ConveyorChains == nullptr)
				{
					ScheduleConveyorSegmentHandoffRetry(Self, TEXT("NetUpdateBuildSplineAfterMissingSubsystem"));
				}
				ScheduleConveyorItemUpdateRetry(Self, TEXT("NetUpdateBuildSplineAfter"));
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGConveyorChainActor, AddClientAvailableConveyor,
			[](AFGConveyorChainActor* Self, AFGBuildableConveyorBase* /*ConveyorBase*/)
			{
				if (!IsValid(Self)) return;
				if (Self->HasAuthority()) return;
				if (Self->HasBuildChainSegments()) return;

				AFGConveyorChainSubsystem* ConveyorChains = GetReadyConveyorChainSubsystem(Self->GetWorld());
				if (ConveyorChains == nullptr)
				{
					ScheduleConveyorSegmentHandoffRetry(Self, TEXT("AddClientAvailableConveyorAfterMissingSubsystem"));
					return;
				}

				ConveyorChains->NotifyChainNeedsSegmentUpdate(Self);
			});

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 14/20/21/22: conveyor animation sync, segment handoff retry, item update retry, and client-available segment nudge hooks installed"));
	}

	int32 WakeItemPickupsNear(UWorld* World, const FVector& Location, float Radius)
	{
		if (!IsValid(World)) return 0;

		const float RadiusSq = Radius * Radius;
		int32 PickupCount = 0;
		for (TActorIterator<AFGItemPickup> It(World); It; ++It)
		{
			AFGItemPickup* Pickup = *It;
			if (!IsValid(Pickup)) continue;
			if (FVector::DistSquared(Pickup->GetActorLocation(), Location) > RadiusSq) continue;

			ForceWakeItemPickup(Pickup);
			++PickupCount;
		}

		return PickupCount;
	}

	bool CreatureHasDeathDropClass(AFGCreature* Creature)
	{
		if (!IsValid(Creature)) return false;

		FProperty* DropProp = Creature->GetClass()->FindPropertyByName(TEXT("mItemToDrop"));
		FClassProperty* ClassProp = CastField<FClassProperty>(DropProp);
		if (ClassProp == nullptr) return false;

		UObject* DropObject = ClassProp->GetObjectPropertyValue_InContainer(Creature);
		UClass* DropClass = Cast<UClass>(DropObject);
		return IsValid(DropClass);
	}

	void VerifyCreatureDeathDrop(TWeakObjectPtr<AFGCreature> WeakCreature, FVector DeathLocation, bool bDidRetry)
	{
		AFGCreature* Creature = WeakCreature.Get();
		if (!IsValid(Creature)) return;
		if (!Creature->HasAuthority()) return;

		UWorld* World = Creature->GetWorld();
		if (!IsValid(World)) return;

		const int32 PickupCount = WakeItemPickupsNear(World, DeathLocation, 1200.0f);
		if (PickupCount > 0)
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 15: woke %d pickup(s) after creature death near %s"),
				PickupCount, *DeathLocation.ToString());
			return;
		}

		if (bDidRetry) return;
		if (!CreatureHasDeathDropClass(Creature))
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 15: no valid mItemToDrop for %s; not retrying SpawnDeathItem"),
				*Creature->GetName());
			return;
		}

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 15: no pickup near creature death; retrying SpawnDeathItem once for %s"),
			*Creature->GetName());
		Creature->SpawnDeathItem();

		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateLambda([WeakCreature, DeathLocation]()
			{
				VerifyCreatureDeathDrop(WeakCreature, DeathLocation, true);
			}),
			2.0f,
			false);
	}

	void InstallCreatureLootHooks()
	{
		using FCreateItemDropSignature = AFGItemPickup_Spawnable* (*)(
			UFGInventoryComponent*,
			UWorld*,
			const FInventoryStack&,
			const FVector&,
			const FRotator&,
			TSubclassOf<AFGItemPickup_Spawnable>,
			ULevel*,
			FName);

		using FAddItemToWorldStackSignature = AFGItemPickup_Spawnable* (*)(
			UFGInventoryComponent*,
			const FInventoryStack&,
			const FVector&,
			const FRotator&,
			TSubclassOf<AFGItemPickup_Spawnable>);

		using FCreateDropsInCylinderSignature = void (*)(
			UWorld*,
			const TArray<FInventoryStack>&,
			FVector,
			float,
			const TArray<AActor*>&,
			TArray<AFGItemPickup_Spawnable*>&,
			TSubclassOf<AFGItemPickup_Spawnable>);

		SUBSCRIBE_METHOD_EXPLICIT_AFTER(FCreateItemDropSignature, AFGItemPickup_Spawnable::CreateItemDrop,
			[](AFGItemPickup_Spawnable* const& CreatedPickup,
				UFGInventoryComponent* /*InventoryComponent*/,
				UWorld* /*World*/,
				const FInventoryStack& /*Item*/,
				const FVector& /*SpawnLocation*/,
				const FRotator& /*SpawnRotation*/,
				TSubclassOf<AFGItemPickup_Spawnable> /*ItemDropClass*/,
				ULevel* /*SpawnLevelOverride*/,
				FName /*SpawnNameOverride*/)
			{
				ForceWakeItemPickup(CreatedPickup);
			});

		SUBSCRIBE_METHOD_EXPLICIT_AFTER(FAddItemToWorldStackSignature, AFGItemPickup_Spawnable::AddItemToWorldStackAtLocation,
			[](AFGItemPickup_Spawnable* const& CreatedPickup,
				UFGInventoryComponent* /*InventoryComponent*/,
				const FInventoryStack& /*Item*/,
				const FVector& /*SpawnLocation*/,
				const FRotator& /*SpawnRotation*/,
				TSubclassOf<AFGItemPickup_Spawnable> /*ItemDropClass*/)
			{
				ForceWakeItemPickup(CreatedPickup);
			});

		SUBSCRIBE_METHOD_EXPLICIT_AFTER(FCreateDropsInCylinderSignature, AFGItemPickup_Spawnable::CreateItemDropsInCylinder,
			[](UWorld* /*World*/,
				const TArray<FInventoryStack>& /*Items*/,
				FVector /*AroundLocation*/,
				float /*SphereRadius*/,
				const TArray<AActor*>& /*ActorsToIgnore*/,
				TArray<AFGItemPickup_Spawnable*>& OutItemDrops,
				TSubclassOf<AFGItemPickup_Spawnable> /*ItemDropClass*/)
			{
				for (AFGItemPickup_Spawnable* Pickup : OutItemDrops)
				{
					ForceWakeItemPickup(Pickup);
				}
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGCreature, Died,
			[](AFGCreature* Self, AActor* /*Died*/)
			{
				if (!IsValid(Self)) return;
				if (!Self->HasAuthority()) return;

				UWorld* World = Self->GetWorld();
				if (!IsValid(World)) return;

				const FVector DeathLocation = Self->GetActorLocation();
				FTimerHandle Handle;
				World->GetTimerManager().SetTimer(
					Handle,
					FTimerDelegate::CreateLambda([WeakCreature = TWeakObjectPtr<AFGCreature>(Self), DeathLocation]()
					{
						VerifyCreatureDeathDrop(WeakCreature, DeathLocation, false);
					}),
					1.0f,
					false);
			});

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 15: creature loot replication hooks installed"));
	}

	// =========================================================================
	// Hook 7: Force-hide stuck client loading screen
	// Symptom: Client successfully joins multiplayer (listen OR dedicated),
	//          enters the world, character is possessed and active (audio
	//          plays for swings, footsteps, etc), input fires gameplay actions
	//          — but the loading-screen UMG widget never dismisses, blocking
	//          all UI input. The host's player list shows the joiner online.
	// Cause:   SF's loading-screen dismissal trigger doesn't fire when the
	//          save has stale subobject references. Concrete evidence:
	//            LogNet: Warning: UActorChannel::ProcessBunch:
	//              ReadContentBlockPayload failed to find/create object.
	//              RepObj: FGInventoryComponent ... InventoryPotential
	//          That replication-layer warning + the MoviePlayer log
	//          "PassLoadingScreenWindowBackToGame failed. No Window" indicates
	//          the loading-screen movie player can't hand control back, and
	//          SF's "ready to dismiss" predicate stays false forever.
	// Fix:     Once the local client has a valid PlayerController with a
	//          possessed Pawn AND the critical subsystems are valid, call
	//          UFGLocalPlayer::ForceHideEarlyLoadingScreen() and stop the
	//          movie player explicitly. Idempotent and safe — the call is
	//          a no-op if the screen is already hidden.
		// =========================================================================
		static FTSTicker::FDelegateHandle GLoadingKillerHandle;
		static const float LOADING_KILLER_INTERVAL = 0.5f;
		static const double LOADING_KILLER_JOIN_GRACE_SECONDS = 6.0;
		static const int32 SLATE_DUMP_MAX_DEPTH = 14;

	// Recursively force-collapse a widget and every descendant. Used
	// after we identify the SFGMinimalLoadingScreen subtree and want to
	// guarantee nothing inside it renders (children's individual visibility
	// is otherwise honoured even if the parent reports Collapsed).
	void ForceCollapseSubtree(TSharedPtr<SWidget> W)
	{
		if (!W.IsValid()) return;
		W->SetVisibility(EVisibility::Collapsed);
		FChildren* Kids = W->GetChildren();
		if (Kids == nullptr) return;
		const int32 N = Kids->Num();
		for (int32 i = 0; i < N; i++)
		{
			ForceCollapseSubtree(Kids->GetChildAt(i));
		}
	}

	// Walk the Slate tree from a window, find any "FGMinimalLoadingScreen"
	// / "FGSpinnerLoader" / "FGLoadingScreen" widget, and force it + all
	// descendants to EVisibility::Collapsed. Returns true if any such
	// widget was found (so we know the kill fired).
	bool KillLoadingScreenSlate(TSharedPtr<SWidget> W)
	{
		if (!W.IsValid()) return false;
		const FString Type = W->GetTypeAsString();
		if (Type.Contains(TEXT("FGMinimalLoadingScreen")) ||
			Type.Contains(TEXT("FGSpinnerLoader")) ||
			Type.Contains(TEXT("FGLoadingScreen")))
		{
			ForceCollapseSubtree(W);
			return true;
		}
		bool bAny = false;
		FChildren* Kids = W->GetChildren();
		if (Kids != nullptr)
		{
			const int32 N = Kids->Num();
			for (int32 i = 0; i < N; i++)
			{
				if (KillLoadingScreenSlate(Kids->GetChildAt(i)))
				{
					bAny = true;
				}
			}
		}
		return bAny;
	}

	// Recursively dump SWidget hierarchy to log so we can identify the
	// loading-screen Slate widget (which lives outside UMG's UUserWidget
	// pool and thus isn't visible to TObjectIterator<UUserWidget>).
	void DumpSlateWidget(TSharedPtr<SWidget> W, int32 Depth)
	{
		if (!W.IsValid() || Depth > SLATE_DUMP_MAX_DEPTH) return;

		FString Pad;
		for (int32 i = 0; i < Depth; i++) Pad += TEXT("  ");

		const EVisibility V = W->GetVisibility();
		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("[SLATE]%s%s vis=%s vis-hidden=%d"),
			*Pad,
			*W->GetTypeAsString(),
			*V.ToString(),
			(int32)(V == EVisibility::Hidden));

		FChildren* Children = W->GetChildren();
		if (Children == nullptr) return;
		const int32 N = Children->Num();
		for (int32 i = 0; i < N; i++)
		{
			DumpSlateWidget(Children->GetChildAt(i), Depth + 1);
		}
	}

	bool LoadingScreenKillerTick(float /*Delta*/)
	{
		if (!GEngine) return true;

		UWorld* World = nullptr;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::Game && Ctx.World() != nullptr)
			{
				World = Ctx.World();
				break;
			}
		}
		if (!World) return true;

		// Server doesn't show loading screens we care about; bail.
		const ENetMode NM = World->GetNetMode();
		if (NM == NM_DedicatedServer) return true;

		APlayerController* PC = World->GetFirstPlayerController();
		if (!IsValid(PC)) return true;

		// Note: don't gate on Pawn or GS being valid — we still want the
		// Slate-hierarchy dump even if the player hasn't been possessed yet,
		// because the loading screen overlay is most likely active in that
		// exact pre-possession window.
		APawn* Pawn = PC->GetPawn();
		AGameStateBase* GS = World->GetGameState();

		// Movement-unfreeze: the "stuck loading screen but actions work
		// (LMB swing / F open deconstruct) only WASD/jump dead" pattern
		// means the CharacterMovementComponent is frozen at MOVE_None.
		// SF disables movement during the loading-screen window and never
		// re-enables it when dismissal fails. Force-restore the input
		// ignore flags AND the movement mode every tick.
		if (IsValid(Pawn) && NM != NM_Standalone)
		{
			static bool bLoggedFreezeState = false;
			if (!bLoggedFreezeState)
			{
				bLoggedFreezeState = true;
				int32 InitialMM = -1;
				if (ACharacter* CharLog = Cast<ACharacter>(Pawn))
				{
					if (UCharacterMovementComponent* CMC = CharLog->GetCharacterMovement())
					{
						InitialMM = (int32)CMC->MovementMode;
					}
				}
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 7: PC freeze probe — IgnoreMoveInput=%d IgnoreLookInput=%d MovementMode=%d (0=None,1=Walking,3=Falling,5=Flying)"),
					(int32)PC->IsMoveInputIgnored(),
					(int32)PC->IsLookInputIgnored(),
					InitialMM);
			}

			PC->ResetIgnoreInputFlags();

			if (ACharacter* Char = Cast<ACharacter>(Pawn))
			{
				if (UCharacterMovementComponent* CMC = Char->GetCharacterMovement())
				{
					if (CMC->MovementMode == MOVE_None)
					{
						CMC->SetMovementMode(MOVE_Walking);
						UE_LOG(LogSinkSubsystemRaceFix, Display,
							TEXT("Hook 7: restored CharacterMovement MovementMode None -> Walking"));
					}
				}
			}

			// IsPlayerOnline() is protected in AFGCharacterPlayer so we
			// can't call it from here. Skip the readback — we'll need to
			// rely on log evidence elsewhere or use an offset-based read
			// of mIsPlayerOnline directly if we want to confirm.
		}

		// Give vanilla/mod respawn and welcome widgets a clean window before
		// any loading-screen fallback starts force-hiding UI. We still run the
		// movement restore above during this window, but we do not touch UMG,
		// Slate loading screens, MoviePlayer, or GameUI loading-state flags.
		if (NM != NM_Standalone && IsValid(Pawn))
		{
			static TMap<FObjectKey, double> GLoadingKillerPawnFirstSeen;
			const FObjectKey ControllerKey(PC);
			const double Now = FPlatformTime::Seconds();
			double* FirstSeen = GLoadingKillerPawnFirstSeen.Find(ControllerKey);
			if (FirstSeen == nullptr)
			{
				GLoadingKillerPawnFirstSeen.Add(ControllerKey, Now);
				return true;
			}
			if ((Now - *FirstSeen) < LOADING_KILLER_JOIN_GRACE_SECONDS)
			{
				return true;
			}
		}

			// Only kill generic loading-screen UMG widgets. Do not touch
			// Widget_Respawn_C anymore: that widget is part of the real respawn /
		// welcome flow for at least one mod setup, and collapsing it can skip
		// objective setup and downstream mod initialization.
		if (NM != NM_Standalone && IsValid(Pawn))
		{
			static bool bLoggedLoadingKill = false;

			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* UW = *It;
				if (!IsValid(UW)) continue;
				if (UW->GetWorld() != World) continue;
				if (!UW->IsInViewport()) continue;
				const FString Cls = UW->GetClass()->GetName();

				const bool bIsLoading = Cls.Contains(TEXT("LoadingScreen"));
				if (!bIsLoading) continue;
				if (UW->GetVisibility() == ESlateVisibility::Collapsed) continue;

				UW->SetVisibility(ESlateVisibility::Collapsed);
				if (!bLoggedLoadingKill)
				{
					bLoggedLoadingKill = true;
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Hook 7: Collapsed stuck loading UMG widget %s (%s)"),
						*Cls, *UW->GetName());
				}
			}
		}

		// Surgical kill: walk the Slate tree and force-collapse the
		// SFGMinimalLoadingScreen subtree (the actual stuck overlay we
		// identified from the depth-14 dump). Runs every tick because
		// SF re-asserts visibility — needs continuous suppression while
		// the broken loading-screen state machine is still "active".
		if (NM != NM_Standalone && FSlateApplication::IsInitialized())
		{
			TArray<TSharedRef<SWindow>> Windows = FSlateApplication::Get().GetInteractiveTopLevelWindows();
			if (TSharedPtr<SWindow> Active = FSlateApplication::Get().GetActiveTopLevelWindow())
			{
				Windows.AddUnique(Active.ToSharedRef());
			}
			static bool bLoggedKill = false;
			for (const TSharedRef<SWindow>& Win : Windows)
			{
				if (KillLoadingScreenSlate(Win) && !bLoggedKill)
				{
					bLoggedKill = true;
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Hook 7: KillLoadingScreenSlate hit — collapsed SFGMinimalLoadingScreen subtree"));
				}
			}
		}

		// Multi-pronged attack — call every API that can dismiss any kind of
		// loading-screen overlay SF has. All idempotent, so running every
		// 0.5s is fine.
		if (UFGLocalPlayer* LP = Cast<UFGLocalPlayer>(PC->GetLocalPlayer()))
		{
			LP->ForceHideEarlyLoadingScreen();
		}

		if (IGameMoviePlayer* MoviePlayer = GetMoviePlayer())
		{
			if (MoviePlayer->IsMovieCurrentlyPlaying())
			{
				MoviePlayer->StopMovie();
			}
		}

		if (AFGPlayerController* FGPC = Cast<AFGPlayerController>(PC))
		{
			if (UFGGameUI* GameUI = FGPC->GetGameUI())
			{
				if (!GameUI->IsFrontEndLoadingScreenFinished())
				{
					GameUI->SetFrontEndLoadingScreenFinished(true);
				}
			}
		}

		// Per-world diagnostic: dump UMG widgets AND walk the Slate window
		// tree on world change. The loading-screen overlay is a Slate-only
		// widget (TSharedPtr<SWidget> mEarlyLoadingScreenWidget) so the UMG
		// dump alone won't show it — the Slate walk will.
		static TWeakObjectPtr<UWorld> LastDumpedWorld;
		if (LastDumpedWorld.Get() != World)
		{
			LastDumpedWorld = World;

			// (Removed: RemoveAllViewportWidgets brute-force — it also kills
			// BP_GameUI_C which holds the EnhancedInput mapping context, so
			// character movement/UI breaks when wiped. Need to target the
			// loading-screen Slate widget surgically once we identify it.)
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 7: dumping all UUserWidget instances currently in viewport ")
				TEXT("— look for the one rendering the stuck loading-screen overlay"));
			int32 Counter = 0;
			for (TObjectIterator<UUserWidget> It; It; ++It)
			{
				UUserWidget* W = *It;
				if (!IsValid(W)) continue;
				if (!W->IsInViewport()) continue;
				if (W->GetWorld() != World) continue;
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("  viewport widget %d: class=%s name=%s vis=%d"),
					Counter++, *W->GetClass()->GetName(), *W->GetName(),
					(int32)W->GetVisibility());
			}
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 7: %d UUserWidget(s) found in viewport"), Counter);

			// Now walk the Slate window tree.  We deliberately use BOTH the
			// "interactive top level" and the "active top level" sources,
			// because the MoviePlayer's loading-screen window is typically
			// not interactive (no input) and is missed by GetInteractiveTopLevelWindows.
			if (FSlateApplication::IsInitialized())
			{
				FSlateApplication& SA = FSlateApplication::Get();

				// Currently focused widget — useful to know what's eating
				// keyboard input (camera-only movement = focused widget is
				// eating WASD before it reaches the input component).
				TSharedPtr<SWidget> FocusedW = SA.GetUserFocusedWidget(0);
				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 7: focused widget for user 0 = %s"),
					FocusedW.IsValid() ? *FocusedW->GetTypeAsString() : TEXT("<none>"));

				TArray<TSharedRef<SWindow>> Windows = SA.GetInteractiveTopLevelWindows();
				if (TSharedPtr<SWindow> Active = SA.GetActiveTopLevelWindow())
				{
					Windows.AddUnique(Active.ToSharedRef());
				}

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Hook 7: Slate windows discovered = %d (max depth %d)"),
					Windows.Num(), SLATE_DUMP_MAX_DEPTH);
				for (const TSharedRef<SWindow>& Win : Windows)
				{
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("[SLATE] === Window: '%s' geom=%s visible=%d ==="),
						*Win->GetTitle().ToString(),
						*Win->GetSizeInScreen().ToString(),
						(int32)Win->IsVisible());
					DumpSlateWidget(Win, 0);
				}

				// Also surface MoviePlayer state, since one of its symptoms
				// is leaving its loading-screen window in a partially-shown
				// state (the "PassLoadingScreenWindowBackToGame failed. No Window"
				// log line we see at startup).
				if (IGameMoviePlayer* MP = GetMoviePlayer())
				{
					UE_LOG(LogSinkSubsystemRaceFix, Display,
						TEXT("Hook 7: MoviePlayer state — IsMoviePlaying=%d IsLoadingFinished=%d"),
						(int32)MP->IsMovieCurrentlyPlaying(),
						(int32)MP->IsLoadingFinished());
				}
			}
		}

		UE_LOG(LogSinkSubsystemRaceFix, Verbose,
			TEXT("Hook 7 tick: PC=%s Pawn=%s — dismissal attempts dispatched"),
			*PC->GetName(),
			IsValid(Pawn) ? *Pawn->GetName() : TEXT("<null>"));

		// Stay registered — the loading screen can re-show on travel/respawn,
		// and SF re-creates it. Polling at 0.5s is cheap and self-healing.
		return true;
	}

	void InstallLoadingScreenKillerHook()
	{
		GLoadingKillerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&LoadingScreenKillerTick),
			LOADING_KILLER_INTERVAL);

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Loading screen killer ticker installed (every %.1fs on client only)"),
			LOADING_KILLER_INTERVAL);
	}

	void InstallSchematicListFilterHook()
	{
		SUBSCRIBE_UOBJECT_METHOD_AFTER(AFGSchematicManager, GetAvailableSchematicsOfTypes,
			[](const AFGSchematicManager* Self, TArray<ESchematicType> /*types*/,
			   TArray<TSubclassOf<UFGSchematic>>& OutSchematics)
			{
				if (!IsValid(Self)) return;
				const int32 Before = OutSchematics.Num();
				OutSchematics.RemoveAll([](const TSubclassOf<UFGSchematic>& Cls)
				{
					return Cls.Get() == nullptr;
				});
				const int32 Removed = Before - OutSchematics.Num();
				if (Removed > 0)
				{
					UE_LOG(LogSinkSubsystemRaceFix, Warning,
						TEXT("Filtered %d null schematic(s) from GetAvailableSchematicsOfTypes ")
						TEXT("(stale save reference to a mod-removed schematic class)"),
						Removed);
				}
			});
	}

	void InstallPlayerStateRegisterHook()
	{
		SUBSCRIBE_UOBJECT_METHOD(AFGPlayerState, RegisterPlayerWithSession,
			[](auto& Scope, AFGPlayerState* Self, bool bWasFromInvite)
			{
				if (!IsValid(Self))
				{
					return;
				}

				// Only intervene on the server. On clients this function is
				// usually a no-op anyway, but be safe.
				if (Self->GetLocalRole() != ROLE_Authority)
				{
					return;
				}

				// Skip SF's broken override entirely.
				Scope.Cancel();

				// Force mIsOnline=true here as well in case Hook 3 didn't
				// see this particular PlayerState (race during late join).
				FProperty* Prop = Self->GetClass()->FindPropertyByName(TEXT("mIsOnline"));
				FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop);
				if (BoolProp != nullptr)
				{
					BoolProp->SetPropertyValue_InContainer(Self, true);
					Self->ForceNetUpdate();
				}

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Cancelled AFGPlayerState::RegisterPlayerWithSession on %s ")
					TEXT("(bWasFromInvite=%d) — bypassing LocalUser+IsOnline gates"),
					*Self->GetName(), (int32)bWasFromInvite);
			});
	}

	// =========================================================================
	// Hook 23: defer AModSubsystem::BeginPlay on the client until
	// AFGGameState::AreClientSubsystemsValid() returns true.
	//
	// Architecturally this is the unified fix for the entire family of
	// race-on-join symptoms (CanGiveAccessToSchematic null-deref, missing
	// HUB/buildable mesh, half-init RecipeManager etc.). On the server
	// AFGGameState::Init() spawns ~21 gameplay subsystems (mEventSubsystem,
	// mSchematicManager, mRecipeManager, mTutorialIntroManager, ...). Clients
	// never spawn these — they arrive as Replicated UPROPERTYs on AFGGameState
	// over the wire, one actor channel at a time. SML's USubsystemActorManager
	// dispatches AModSubsystem::Init/BeginPlay before that replication has
	// finished, so every mod's Init code touches null pointers.
	//
	// AFGGameState exposes AreClientSubsystemsValid() (FGGameState.h:119) — a
	// boolean that's true only when every replicated manager is non-null. We
	// gate AModSubsystem::BeginPlay against this boolean on the client side,
	// queuing actors that show up early and replaying their BeginPlay once the
	// signal flips. Server / standalone / listen-server paths are untouched.
	//
	// We hook BeginPlay (not RegisterSubsystemActor / SpawnSubsystemActor)
	// because the actor itself must be spawned on time — SML internals like
	// SessionSettingsSubsystem and ChatCommandSubsystem are looked up by RCO
	// handlers via TActorIterator at RPC-receive time, and their absence
	// triggers a checkf() crash. Deferring only BeginPlay (and therefore
	// AModSubsystem::DispatchInit, which BeginPlay calls) lets the actor
	// exist and be findable, while postponing only the FactoryGame-manager-
	// touching Init() body until the managers are real.
	// =========================================================================

	static TArray<TWeakObjectPtr<AModSubsystem>> GDeferredModSubsystemBeginPlays;
	static FTSTicker::FDelegateHandle GDeferredModSubsystemBeginPlayHandle;

	AFGGameState* GetFactoryGameState(UWorld* World)
	{
		return IsValid(World) ? Cast<AFGGameState>(World->GetGameState()) : nullptr;
	}

	bool AreClientModSubsystemPrerequisitesReady(UWorld* World)
	{
		if (!IsValid(World)) return false;
		if (World->GetNetMode() != NM_Client) return true;
		if (!IsValid(GetFactoryGameState(World))) return false;

		// Do not wait for AFGGameState::AreClientSubsystemsValid() here. On
		// heavily-modded saves several late/optional managers can remain null
		// long after the player is in world, which turned Hook 23 into a broad
		// 30s mod-init stall. These are the managers that repeatedly proved
		// necessary for recipes, schematics, buildables, map/objective, and
		// lightweight/conveyor client setup.
		return IsValid(AFGRecipeManager::Get(World))
			&& IsValid(AFGSchematicManager::Get(World))
			&& IsValid(AFGResourceSinkSubsystem::Get(World))
			&& IsValid(AFGBuildableSubsystem::Get(World))
			&& IsValid(AFGLightweightBuildableSubsystem::Get(World))
			&& IsValid(AFGActorRepresentationManager::Get(World))
			&& IsValid(AFGMapManager::Get(World))
			&& IsValid(AFGConveyorChainSubsystem::Get(World))
			&& IsValid(AFGConveyorItemSubsystem::Get(World));
	}

	bool ShouldGateClientModSubsystemBeginPlay(AModSubsystem* Sub)
	{
		if (!IsValid(Sub))
		{
			return false;
		}

		UWorld* World = Sub->GetWorld();
		if (!IsValid(World) || World->GetNetMode() != NM_Client)
		{
			return false;
		}

		return !AreClientModSubsystemPrerequisitesReady(World);
	}

	void EnsureDeferredModSubsystemBeginPlayTicker();

	void QueueDeferredModSubsystemBeginPlay(AModSubsystem* Sub)
	{
		if (!IsValid(Sub))
		{
			return;
		}

		// Dedupe — the hook may fire more than once if the engine retries
		// BeginPlay for a level-streaming actor.
		for (const TWeakObjectPtr<AModSubsystem>& Existing : GDeferredModSubsystemBeginPlays)
		{
			if (Existing.Get() == Sub)
			{
				return;
			}
		}

		GDeferredModSubsystemBeginPlays.Emplace(Sub);

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 23: deferred AModSubsystem::BeginPlay for %s until join-critical client subsystems are valid"),
			*Sub->GetClass()->GetPathName());

		EnsureDeferredModSubsystemBeginPlayTicker();
	}

	// After this many tick cycles (each 0.1s), force-replay deferred BeginPlays
	// even if the soft prerequisite set is still false. This hook should be a
	// short race guard, not a long mod-init pause.
	static constexpr int32 FORCE_REPLAY_TICK_BUDGET = 50;  // 5 seconds at 0.1s tick

	// Periodic-status log cadence — every 50 ticks (~5s) print which slot is
	// still null so we can see what is delaying the gate.
	static constexpr int32 STATUS_LOG_TICK_INTERVAL = 50;

	static int32 GDeferredModSubsystemBeginPlayTickCount = 0;

	void LogMissingClientSubsystems(AFGGameState* GameState)
	{
		if (!IsValid(GameState))
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 23: still waiting — AFGGameState is null on client"));
			return;
		}

		// Mirror of decompiled AFGGameState::AreClientSubsystemsValid; report
		// each missing slot by name. We use FName lookups via reflection so we
		// don't have to leak each manager type into our header set — the
		// majority are already included transitively, but a few (e.g.
		// AFGStorySubsystem) aren't worth dragging in for a diagnostic.
		static const TCHAR* const FieldNames[] = {
			TEXT("mStorySubsystem"), TEXT("mRailroadSubsystem"), TEXT("mCircuitSubsystem"),
			TEXT("mRecipeManager"), TEXT("mSchematicManager"), TEXT("mGamePhaseManager"),
			TEXT("mResearchManager"), TEXT("mTutorialIntroManager"), TEXT("mPipeSubsystem"),
			TEXT("mActorRepresentationManager"), TEXT("mMapManager"), TEXT("mChatManager"),
			TEXT("mResourceSinkSubsystem"), TEXT("mVehicleSubsystem"), TEXT("mEventSubsystem"),
			TEXT("mDroneSubsystem"), TEXT("mSignSubsystem"), TEXT("mScannableSubsystem"),
			TEXT("mCreatureSubsystem"), TEXT("mBlueprintSubsystem"), TEXT("mGameRulesSubsystem"),
			TEXT("mIconDatabaseSubsystem"), TEXT("mWorldEventSubsystem"), TEXT("mCentralStorageSubsystem"),
		};

		FString Missing;
		UClass* GSClass = GameState->GetClass();
		for (const TCHAR* FieldName : FieldNames)
		{
			FProperty* Prop = GSClass->FindPropertyByName(FName(FieldName));
			if (Prop == nullptr) continue;
			FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop);
			if (ObjProp == nullptr) continue;
			UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(GameState);
			if (Value == nullptr)
			{
				if (!Missing.IsEmpty()) Missing += TEXT(", ");
				Missing += FieldName;
			}
		}

		if (Missing.IsEmpty())
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 23: AreClientSubsystemsValid returned false but all named slots non-null — likely an unlisted subsystem; %d still deferred"),
				GDeferredModSubsystemBeginPlays.Num());
		}
		else
		{
			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 23: still waiting — %d deferred, missing on AFGGameState: %s"),
				GDeferredModSubsystemBeginPlays.Num(), *Missing);
		}
	}

	bool DeferredModSubsystemBeginPlayTick(float /*DeltaTime*/)
	{
		++GDeferredModSubsystemBeginPlayTickCount;
		const bool bForceReplay = GDeferredModSubsystemBeginPlayTickCount >= FORCE_REPLAY_TICK_BUDGET;

		AFGGameState* StatusGameState = nullptr;

		for (int32 Index = GDeferredModSubsystemBeginPlays.Num() - 1; Index >= 0; --Index)
		{
			AModSubsystem* Sub = GDeferredModSubsystemBeginPlays[Index].Get();
			if (!IsValid(Sub))
			{
				GDeferredModSubsystemBeginPlays.RemoveAtSwap(Index);
				continue;
			}

			UWorld* World = Sub->GetWorld();
			if (!IsValid(World) || World->GetNetMode() != NM_Client)
			{
				GDeferredModSubsystemBeginPlays.RemoveAtSwap(Index);
				continue;
			}

			AFGGameState* GameState = GetFactoryGameState(World);
			StatusGameState = GameState;
			const bool bPrerequisitesReady = AreClientModSubsystemPrerequisitesReady(World);
			if (!bPrerequisitesReady && !bForceReplay)
			{
				continue;
			}

			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 23: replaying AActor::DispatchBeginPlay for %s (%s)"),
				*Sub->GetClass()->GetPathName(),
				bPrerequisitesReady
					? TEXT("join-critical client subsystems became valid")
					: TEXT("FORCE-REPLAY after soft-gate timeout — Hook 2/27 fallbacks still active"));

			// Calling DispatchBeginPlay re-enters our hook, but ShouldGate
			// now returns false (subsystems are valid OR timeout passed), so
			// the original runs and dispatches BeginPlay -> AModSubsystem::BeginPlay
			// -> Init(). We use DispatchBeginPlay (public on AActor) instead
			// of BeginPlay (protected on AModSubsystem).
			Sub->DispatchBeginPlay();

			GDeferredModSubsystemBeginPlays.RemoveAtSwap(Index);
		}

		// Periodic status — every ~5s while we're still waiting.
		if (!GDeferredModSubsystemBeginPlays.IsEmpty()
			&& (GDeferredModSubsystemBeginPlayTickCount % STATUS_LOG_TICK_INTERVAL) == 0)
		{
			LogMissingClientSubsystems(StatusGameState);
		}

		if (GDeferredModSubsystemBeginPlays.Num() == 0)
		{
			GDeferredModSubsystemBeginPlayTickCount = 0;
			GDeferredModSubsystemBeginPlayHandle.Reset();
			return false;
		}

		return true;
	}

	void EnsureDeferredModSubsystemBeginPlayTicker()
	{
		if (GDeferredModSubsystemBeginPlayHandle.IsValid())
		{
			return;
		}

		GDeferredModSubsystemBeginPlayHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateStatic(&DeferredModSubsystemBeginPlayTick),
			0.1f);
	}

	void InstallModSubsystemBeginPlayGateHook()
	{
		// Hook AActor::DispatchBeginPlay (public on AActor) instead of
		// AModSubsystem::BeginPlay (protected on AModSubsystem — would fail
		// the C++ access check at the SUBSCRIBE macro's template parameter).
		// DispatchBeginPlay is the public entry that calls the virtual
		// BeginPlay; intercepting it cancels the entire BeginPlay chain
		// (including AActor::BeginPlay -> AFGSubsystem::BeginPlay ->
		// AModSubsystem::BeginPlay -> DispatchInit -> Init()) for the
		// AModSubsystem subset only, leaving every other actor's BeginPlay
		// path untouched.
		SUBSCRIBE_UOBJECT_METHOD(AActor, DispatchBeginPlay,
			[](auto& Scope, AActor* Self, bool /*bFromLevelStreaming*/)
			{
				AModSubsystem* Sub = Cast<AModSubsystem>(Self);
				if (Sub == nullptr)
				{
					return;
				}
				if (!ShouldGateClientModSubsystemBeginPlay(Sub))
				{
					return;
				}

				Scope.Cancel();
				QueueDeferredModSubsystemBeginPlay(Sub);
			});

		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 23 installed: AModSubsystem DispatchBeginPlay deferred on client until join-critical subsystems are valid"));
	}
}

// Hook 26 (v1.6.0): override the hardcoded RepGraph actor-discovery budget.
// UFGReplicationGraph::InitGlobalGraphNodes calls
// UReplicationGraph::SetActorDiscoveryBudget(this, 20) — i.e. 20 kBps — which
// is starvation-level for heavily-modded saves (actors trickle in for
// minutes after the join completes). We after-hook InitGlobalGraphNodes and
// re-call SetActorDiscoveryBudget with a CVar-tunable value so server admins
// can dial it to whatever their connection tolerates without a rebuild.
//
// Both the master toggle and the budget value are CVars — no new hardcoded
// magic numbers, just defaults. The clamp range exists only as a safety
// rail to avoid a fat-fingered admin setting a degenerate value.
static int32 GRepGraphBudgetEnable = 1;
static FAutoConsoleVariableRef CVarRepGraphBudgetEnable(
	TEXT("r.SinkRaceFix.RepGraph.Enable"),
	GRepGraphBudgetEnable,
	TEXT("Master toggle for the actor-discovery budget override (default 1). 0 = leave vanilla 20 kBps in place."),
	ECVF_Default);

// 500 kBps is the starting-point default; sweep 200/500/1000 in testing and
// adjust. The CVar is the configurability surface — this value is just the
// number applied if the admin doesn't override it.
static int32 GRepGraphActorDiscoveryBudgetKBps = 500;
static FAutoConsoleVariableRef CVarRepGraphActorDiscoveryBudgetKBps(
	TEXT("r.SinkRaceFix.RepGraph.ActorDiscoveryBudget"),
	GRepGraphActorDiscoveryBudgetKBps,
	TEXT("Actor discovery budget in kBps applied after UFGReplicationGraph::InitGlobalGraphNodes. Vanilla hardcodes 20. Default 500. Clamped 20..2000 at apply time."),
	ECVF_Default);

void InstallRepGraphActorDiscoveryBudgetHook()
{
	if (GRepGraphBudgetEnable == 0)
	{
		UE_LOG(LogSinkSubsystemRaceFix, Display,
			TEXT("Hook 26: skipped — r.SinkRaceFix.RepGraph.Enable=0"));
		return;
	}

	UFGReplicationGraph* Sample = GetMutableDefault<UFGReplicationGraph>();
	if (!IsValid(Sample))
	{
		UE_LOG(LogSinkSubsystemRaceFix, Warning,
			TEXT("Hook 26: GetMutableDefault<UFGReplicationGraph> returned null — hook not installed"));
		return;
	}

	SUBSCRIBE_METHOD_VIRTUAL_AFTER(UFGReplicationGraph::InitGlobalGraphNodes, Sample,
		[](UFGReplicationGraph* This)
		{
			if (!IsValid(This)) return;
			if (GRepGraphBudgetEnable == 0) return;

			const int32 Budget = FMath::Clamp(GRepGraphActorDiscoveryBudgetKBps, 20, 2000);
			This->SetActorDiscoveryBudget(Budget);

			UE_LOG(LogSinkSubsystemRaceFix, Display,
				TEXT("Hook 26: SetActorDiscoveryBudget overridden to %d kBps (vanilla=20) on %s"),
				Budget,
				*This->GetName());
		});

	UE_LOG(LogSinkSubsystemRaceFix, Display,
		TEXT("Hook 26: RepGraph actor-discovery budget override installed (default %d kBps, tunable via r.SinkRaceFix.RepGraph.ActorDiscoveryBudget)"),
		GRepGraphActorDiscoveryBudgetKBps);
}

// Diagnostic hooks live in DiagnosticHooks.cpp. Forward-declared here so
// StartupModule can install them. All output is gated by CVar
// r.SinkRaceFix.Trace (default 0 = silent), so shipping this in the production
// DLL has no behavioural cost when disabled.
namespace SinkRaceFixDiag
{
	void InstallDiagnosticHooks();
	void UninstallDiagnosticHooks();
}

void FSinkSubsystemRaceFixModule::StartupModule()
{
	UE_LOG(LogSinkSubsystemRaceFix, Display, TEXT("%s starting up"), MOD_NAME);

#if !WITH_EDITOR
	InstallSinkHook();
	InstallSchematicHook();
	InstallPlayerStateBeginPlayHook();
	InstallPlayerStateRegisterHook();
	InstallSchematicListFilterHook();
	InstallSubsystemKickHook();
	InstallRecipeManagerNullHook();
	InstallMovementModeBlockHook();
	InstallRespawnFreezeBlockHook();
	InstallOnlineStateHook();
	InstallJoinCompletionHook();
	InstallRepProxyClientWiringHook();
	InstallLightweightBuildableResolveRepairHook();
	InstallBuildableVisibilityCensusHook();
	InstallRepGraphActorDiscoveryBudgetHook();
	InstallForcedJoinNetSpeedHook();
	InstallManagerReplicationDiagnosticsHook();
	InstallModSubsystemBeginPlayGateHook();
	InstallConveyorAnimationHooks();
	InstallCreatureLootHooks();
	InstallLoadingScreenKillerHook();
	SinkRaceFixDiag::InstallDiagnosticHooks();
#endif
}

void FSinkSubsystemRaceFixModule::ShutdownModule()
{
	UE_LOG(LogSinkSubsystemRaceFix, Display, TEXT("%s shutting down"), MOD_NAME);
#if !WITH_EDITOR
	SinkRaceFixDiag::UninstallDiagnosticHooks();
#endif
}

IMPLEMENT_GAME_MODULE(FSinkSubsystemRaceFixModule, SinkSubsystemRaceFix);
