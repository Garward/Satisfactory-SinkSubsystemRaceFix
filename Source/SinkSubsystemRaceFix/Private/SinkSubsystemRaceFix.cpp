#include "SinkSubsystemRaceFix.h"

#include "Buildables/FGBuildableResourceSink.h"
#include "FGResourceSinkSubsystem.h"
#include "FGSchematic.h"
#include "FGSchematicManager.h"
#include "Patching/NativeHookManager.h"
#include "TimerManager.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogSinkSubsystemRaceFix);

#define MOD_NAME TEXT("SinkSubsystemRaceFix")

// Sink-defer retry budget: 50 * 0.1s = 5s.
static const int32 SINK_MAX_RETRIES = 50;
static const float SINK_RETRY_INTERVAL = 0.1f;

namespace
{
	// =========================================================================
	// Hook 1: AFGBuildableResourceSink::BeginPlay
	// Crash: Assertion failed: mResourceSinkSubsystem [FGBuildableResourceSink.cpp:25]
	// Cause: Sink BeginPlay runs on client before AFGResourceSinkSubsystem has
	//        replicated. Original code asserts on the null pointer.
	// Fix:   Cancel original BeginPlay, retry on a 0.1s timer until subsystem
	//        is non-null or budget exhausted.
	// =========================================================================
	void DeferredSinkBeginPlay(TWeakObjectPtr<AFGBuildableResourceSink> WeakSink, int32 Attempt);

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
	void InstallSchematicHook()
	{
		SUBSCRIBE_METHOD(UFGSchematic::CanGiveAccessToSchematic,
			[](auto& Scope, TSubclassOf<UFGSchematic> InSchematic, UObject* WorldContext)
			{
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

				AFGSchematicManager* Manager = AFGSchematicManager::Get(World);
				if (IsValid(Manager))
				{
					return;
				}

				UE_LOG(LogSinkSubsystemRaceFix, Display,
					TEXT("Short-circuiting UFGSchematic::CanGiveAccessToSchematic ")
					TEXT("(SchematicManager not replicated yet); returning false"));

				Scope.Override(false);
			});
	}
}

void FSinkSubsystemRaceFixModule::StartupModule()
{
	UE_LOG(LogSinkSubsystemRaceFix, Display, TEXT("%s starting up"), MOD_NAME);

#if !WITH_EDITOR
	InstallSinkHook();
	InstallSchematicHook();
#endif
}

void FSinkSubsystemRaceFixModule::ShutdownModule()
{
	UE_LOG(LogSinkSubsystemRaceFix, Display, TEXT("%s shutting down"), MOD_NAME);
}

IMPLEMENT_GAME_MODULE(FSinkSubsystemRaceFixModule, SinkSubsystemRaceFix);
