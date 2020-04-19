// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "MASFunctionLibrary.generated.h"


class UAnimSequence;
class UMirrorTable;


UCLASS(MinimalAPI, meta = (ScriptName = "MirrorLibrary"))
class UMASFunctionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_UCLASS_BODY()
public:
    UFUNCTION(BlueprintCallable, Category = "Mirror Animation", meta = (DevelopmentOnly))
    static MIRRORANIMATIONSYSTEMDEV_API void BulkMirrorEditorOnly(const TArray <UAnimSequence*> Anims, const UMirrorTable* MirrorTable, TArray <UAnimSequence*>& OutNewAnims);
#if WITH_EDITOR
    static MIRRORANIMATIONSYSTEMDEV_API void CreateMirrorSequenceFromAnimSequence(UAnimSequence* MirrorSequence, const UMirrorTable* MirrorTable);
#endif
};
