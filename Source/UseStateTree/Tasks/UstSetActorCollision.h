﻿// SPDX-FileCopyrightText: 2025 NTY.studio
#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"

#include "UseStateTree/Tasks/UstStateTreeTaskCommonBase.h"

#include "UstSetActorCollision.generated.h"

USTRUCT(BlueprintType, meta=(Hidden, Category="Internal"))
struct FUstSetActorCollisionData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="Context")
	TObjectPtr<AActor> Actor;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bTargetCollisionEnabled = true;

	// If true when exiting the state, the component active state will be set to the opposite of the target state
	UPROPERTY(BlueprintReadWrite, EditAnywhere)
	bool bRevertOnExit = false;

};

USTRUCT(BlueprintType, DisplayName="Set Actor Collision [UST]", meta=(Category="Actor"))
struct USESTATETREE_API FUstSetActorCollision : public FUstStateTreeTaskCommonBase
{
	GENERATED_BODY()

public:
	using FInstanceDataType = FUstSetActorCollisionData;
	virtual UStruct const* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, FStateTreeTransitionResult const& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, FStateTreeTransitionResult const& Transition) const override;

#if WITH_EDITOR
	virtual FText GetDescription(
		FGuid const& ID,
		FStateTreeDataView InstanceDataView,
		IStateTreeBindingLookup const& BindingLookup,
		EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text
	) const override;

	virtual FName GetIconName() const override
	{
		return FName("EditorStyle|ShowFlagsMenu.Collision");
	}

#endif
};