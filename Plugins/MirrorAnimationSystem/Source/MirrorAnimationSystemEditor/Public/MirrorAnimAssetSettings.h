// Copyright 2017-2018 Theodor Luis Martin Bruna (Hethger). All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "MirrorTable.h"
#include "MirrorAnimAssetSettings.generated.h"
/**
 * 
 */
/*class used in Mirror AnimAsset Dialog for choosing the Mirror Table asset in a details view*/
UCLASS()
class MIRRORANIMATIONSYSTEMEDITOR_API UMirrorAnimAssetSettings : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(Category = Settings, EditAnywhere, meta = (HideAlphaChannel))
		UMirrorTable* MirrorTable;
	UMirrorAnimAssetSettings(const FObjectInitializer& ObjectInitializer);
};
