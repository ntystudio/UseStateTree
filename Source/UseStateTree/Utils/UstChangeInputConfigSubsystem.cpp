﻿// Fill out your copyright notice in the Description page of Project Settings.

#include "UstChangeInputConfigSubsystem.h"

#include "TimerManager.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"

#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"

#include "UseStateTree/UseStateTree.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(UstChangeInputConfigSubsystem)

DEFINE_LOG_CATEGORY(LogUstChangeInputConfigSubsystem)

#define CICS_LOG(Verbosity, ...) UE_LOG(LogUstChangeInputConfigSubsystem, Verbosity, ##__VA_ARGS__)
#define CICS_CLOG(Cond, Verbosity, ...) UE_CLOG(Cond, LogUstChangeInputConfigSubsystem, Verbosity, ##__VA_ARGS__)

FString LexToStringOptionalHandle(TOptional<FGuid> const& Guid)
{
	return Guid.IsSet() ? Guid.GetValue().ToString() : TEXT("None");
}

UUstChangeInputConfigSubsystem* UUstChangeInputConfigSubsystem::Get(ULocalPlayer const* LocalPlayer)
{
	return IsValid(LocalPlayer) ? LocalPlayer->GetSubsystem<UUstChangeInputConfigSubsystem>() : nullptr;
}

UUstChangeInputConfigSubsystem* UUstChangeInputConfigSubsystem::Get(APlayerController const* PC)
{
	return Get(IsValid(PC) ? PC->GetLocalPlayer() : nullptr);
}

void UUstChangeInputConfigSubsystem::Deinitialize()
{
	if (auto const World = GetWorld())
	{
		World->GetTimerManager().ClearAllTimersForObject(this);
	}
	InputConfigHandleStack.Empty();
	InputConfigs.Empty();
	CurrentInputConfigHandle.Reset();
	OnInputConfigEnqueued.Clear();
	OnInputConfigChanged.Clear();
	EnqueuedInputConfigs.Empty();
	Super::Deinitialize();
}

TOptional<FUstInputModeConfig> UUstChangeInputConfigSubsystem::GetInputConfig(TOptional<FGuid> const& InputConfigHandle) const
{
	if (!InputConfigHandle.IsSet()) return {};
	FGuid const Key = InputConfigHandle.GetValue();
	if (!InputConfigs.Contains(Key))
	{
		ensure(!InputConfigHandleStack.Contains(Key));
		ensure(!EnqueuedInputConfigs.Contains(Key));
		return {};
	}
	return InputConfigs[Key];
}

FString UUstChangeInputConfigSubsystem::DescribeHandle(TOptional<FGuid> InputConfigHandle) const
{
	auto const InputConfig = GetInputConfig(InputConfigHandle);
	if (!InputConfig.IsSet()) return TEXT("None");
	return UEnum::GetDisplayValueAsText(InputConfig->InputMode).ToString();
}

FGuid UUstChangeInputConfigSubsystem::PushInputConfig(FUstInputModeConfig const& InputConfig)
{
	FGuid const InputConfigHandle = FGuid::NewGuid();
	InputConfigHandleStack.Push(InputConfigHandle);
	InputConfigs.Add(InputConfigHandle, InputConfig);
	EnqueuedInputConfigs.Add(InputConfigHandle);
	ScheduleUpdate();
	return InputConfigHandle;
}

void UUstChangeInputConfigSubsystem::PopInputConfig(FGuid const& InputConfigHandle)
{
	if (!ensure(InputConfigHandleStack.Contains(InputConfigHandle))) return;
	EnqueuedInputConfigs.Remove(InputConfigHandle);
	InputConfigHandleStack.Remove(InputConfigHandle);
	ensure(InputConfigs.Contains(InputConfigHandle));
	InputConfigs.Remove(InputConfigHandle);
	ScheduleUpdate();
}

TOptional<FGuid> UUstChangeInputConfigSubsystem::PeekInputConfigStack() const
{
	if (!InputConfigHandleStack.Num()) return {};
	return InputConfigHandleStack.Last();
}

TOptional<FUstInputModeConfig> UUstChangeInputConfigSubsystem::GetCurrentInputConfig() const
{
	if (!CurrentInputConfigHandle.IsSet() || !InputConfigs.Contains(CurrentInputConfigHandle.GetValue())) return {};
	return InputConfigs[CurrentInputConfigHandle.GetValue()];
}

void UUstChangeInputConfigSubsystem::ScheduleUpdate()
{
	if (GetWorld()->GetTimerManager().IsTimerActive(UpdateHandle)) return;
	UpdateHandle = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &UUstChangeInputConfigSubsystem::Update);
}

FUstInputModeConfig UUstChangeInputConfigSubsystem::GetInputConfigFromStack() const
{
	TOptional<FUstInputModeConfig> InputConfigContext;
	for (auto const InputConfigHandle : InputConfigHandleStack)
	{
		if (!InputConfigHandle.IsValid()) continue;
		auto const InputConfig = GetInputConfig(InputConfigHandle);
		if (!InputConfig.IsSet()) continue;
		if (!InputConfigContext.IsSet())
		{
			InputConfigContext = InputConfig;
			continue;
		}

		InputConfigContext->InputMode = InputConfig->InputMode;
		InputConfigContext->bOverrideInputModeDefault = InputConfig->bOverrideInputModeDefault;
		if (InputConfig->InputMode == EUstInputMode::GameOnly)
		{
			InputConfigContext->bFlushInput = InputConfig->bFlushInput;
		}
		if (InputConfig->OverridesIgnoreLookInput())
		{
			InputConfigContext->IgnoreInputConfig.bOverrideIgnoreLookInput = true;
			InputConfigContext->IgnoreInputConfig.bIgnoreLookInput = InputConfig->IgnoreLookInput();
		}
		if (InputConfig->OverrideIgnoreMoveInput())
		{
			InputConfigContext->IgnoreInputConfig.bOverrideIgnoreMoveInput = true;
			InputConfigContext->IgnoreInputConfig.bIgnoreMoveInput = InputConfig->IgnoreMoveInput();
		}
		if (InputConfig->bOverrideInputModeDefault && InputConfig->InputMode != EUstInputMode::GameOnly)
		{
			InputConfigContext->bShowMouseCursor = InputConfig->bShowMouseCursor;
		}

		if (InputConfig->OverridesMouseCursor())
		{
			InputConfigContext->bOverrideMouseCursor = true;
			InputConfigContext->MouseCursor = InputConfig->MouseCursor;
		}

		if (InputConfig->OverrideMouseCaptureLock())
		{
			InputConfigContext->bOverrideMouseCaptureLock = true;
			InputConfigContext->MouseLockMode = InputConfig->MouseLockMode;
			InputConfigContext->MouseCaptureMode = InputConfig->MouseCaptureMode;
			InputConfigContext->bHideCursorDuringCapture = InputConfig->HideCursorDuringCapture();
		}

		if (InputConfig->InputMode != EUstInputMode::GameOnly)
		{
			InputConfigContext->WidgetToFocus = InputConfig->WidgetToFocus;
		}
	}
	if (!InputConfigContext.IsSet()) return {};
	return InputConfigContext.GetValue();
}

void UUstChangeInputConfigSubsystem::ApplyInputConfigFromHandle(TOptional<FGuid> InputConfigHandle)
{
	auto const LocalPlayer = GetLocalPlayer();
	if (!LocalPlayer) return;
	auto const PC = LocalPlayer->GetPlayerController(GetWorld());
	if (!IsValid(PC)) return;
	if (CurrentInputConfigHandle == InputConfigHandle) { return; } // already applied
	CICS_LOG(
		Verbose,
		TEXT("ApplyInputConfigFromHandle: Changing Input Config: %s <- %s"),
		*DescribeHandle(InputConfigHandle),
		*DescribeHandle(CurrentInputConfigHandle)
	);
	CurrentInputConfigHandle = InputConfigHandle;
	if (!GetInputConfig(InputConfigHandle))
	{
		CICS_LOG(Log, TEXT("ApplyInputConfigFromHandle: Empty Input Config"));
	}

	auto InputConfig = GetInputConfigFromStack();

	if (InputConfig.OverridesIgnoreLookInput())
	{
		bool const bIgnoreLookInput = InputConfig.IgnoreLookInput();
		if (bIsIgnoringLookInput != bIgnoreLookInput)
		{
			bIsIgnoringLookInput = bIgnoreLookInput;
			PC->SetIgnoreLookInput(bIsIgnoringLookInput);
		}
	}
	if (InputConfig.OverrideIgnoreMoveInput())
	{
		bool const bIgnoreMoveInput = InputConfig.IgnoreMoveInput();
		if (bIsIgnoringMoveInput != bIgnoreMoveInput)
		{
			bIsIgnoringMoveInput = bIgnoreMoveInput;
			PC->SetIgnoreMoveInput(bIsIgnoringMoveInput);
		}
	}

	auto SetMouseCursor = [InputConfig, PC, LocalPlayer, this]()
	{
		if (InputConfig.OverridesMouseCursor())
		{
			PC->CurrentMouseCursor = static_cast<EMouseCursor::Type>(InputConfig.MouseCursor);
		}
		PC->SetShowMouseCursor(InputConfig.bShowMouseCursor);
		auto const GameViewportClient = LocalPlayer->ViewportClient;
		GameViewportClient->SetHideCursorDuringCapture(InputConfig.bHideCursorDuringCapture);
		GameViewportClient->SetMouseLockMode(InputConfig.MouseLockMode);
	};

	// prevent reporting as enqueued
	// EnqueuedInputConfigs.Remove(InputConfigHandle.GetValue());
	switch (InputConfig.InputMode)
	{
		case EUstInputMode::GameOnly:
		{
			UWidgetBlueprintLibrary::SetInputMode_GameOnly(PC, InputConfig.bFlushInput);
			if (!InputConfig.bOverrideInputModeDefault)
			{
				PC->SetShowMouseCursor(false);
			}
			else
			{
				SetMouseCursor();
			}
			break;
		}
		case EUstInputMode::GameAndUI:
		{
			UWidgetBlueprintLibrary::SetInputMode_GameAndUIEx(PC, InputConfig.WidgetToFocus, InputConfig.MouseLockMode, InputConfig.bHideCursorDuringCapture);
			SetMouseCursor();
		}
		case EUstInputMode::UIOnly:
		{
			UWidgetBlueprintLibrary::SetInputMode_UIOnlyEx(PC, InputConfig.WidgetToFocus, InputConfig.MouseLockMode, InputConfig.bFlushInput);
			SetMouseCursor();
			break;
		}
		default:
		{
			CICS_LOG(Error, TEXT("ApplyInputConfigFromHandle: Unknown InputMode %s"), *UEnum::GetDisplayValueAsText(InputConfig.InputMode).ToString());
		}
	}
}

void UUstChangeInputConfigSubsystem::Update()
{
	auto const PreviousInputConfigHandle = CurrentInputConfigHandle;
	ApplyInputConfigFromHandle(PeekInputConfigStack());
	ensure(CurrentInputConfigHandle == PeekInputConfigStack());

	ensure(CurrentInputConfigHandle.IsSet() || EnqueuedInputConfigs.Num() == 0); // shouldn't have enqueued configs if we're not applying anything
	auto CurrentEnqueuedInputConfigs = EnqueuedInputConfigs;
	EnqueuedInputConfigs.Reset();
	for (auto const InputConfigHandle : CurrentEnqueuedInputConfigs)
	{
		ensure(GetInputConfig(InputConfigHandle).IsSet());
		OnInputConfigEnqueued.Broadcast(InputConfigHandle);
	}
}
