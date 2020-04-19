// Copyright 2017 UNAmedia. All Rights Reserved.

#include "MixamoAnimationRootMotionSolver.h"

#include "MixamoToolkitPrivatePCH.h"

#include "MixamoToolkitPrivate.h"

#include "Editor.h"
#include "SMixamoToolkitWidget.h"
#include "Misc/MessageDialog.h"

#include "Animation/AnimSequence.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

#include "AssetRegistryModule.h"

#define LOCTEXT_NAMESPACE "FMixamoAnimationRetargetingModule"

void FMixamoAnimationRootMotionSolver::LaunchProcedureFlow(USkeleton* Skeleton)
{
    checkf(Skeleton != nullptr, TEXT("A reference skeleton must be specified."));
    checkf(CanExecuteProcedure(Skeleton), TEXT("Incompatible skeleton."));

    TSharedRef<SWindow> WidgetWindow = SNew(SWindow)
        .Title(LOCTEXT("FMixamoAnimationRootMotionSolver_AskUserForAnimations_WindowTitle", "Select animations"))
        .ClientSize(FVector2D(500, 600))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        .HasCloseButton(false);

    TSharedRef<SRootMotionExtractionWidget> RootMotionExtractionWidget = SNew(SRootMotionExtractionWidget)
        .ReferenceSkeleton(Skeleton);

    WidgetWindow->SetContent(RootMotionExtractionWidget);

    GEditor->EditorAddModalWindow(WidgetWindow);

    UAnimSequence* SelectedAnimation = RootMotionExtractionWidget->GetSelectedAnimation();
    UAnimSequence* SelectedInPlaceAnimation = RootMotionExtractionWidget->GetSelectedInPlaceAnimation();

    if (!SelectedAnimation || !SelectedInPlaceAnimation)
        return;

    // check, with an heuristic, that the user has selected the right "IN PLACE" animation otherwise prompt a message box as warning
    const UAnimSequence* EstimatedInPlaceAnim = EstimateInPlaceAnimation(SelectedAnimation, SelectedInPlaceAnimation);
    if (EstimatedInPlaceAnim != SelectedInPlaceAnimation)
    {
        FText WarningText = LOCTEXT("SRootMotionExtractionWidget_InPlaceAnimWarning", "Warning: are you sure to have choose the right IN PLACE animation?");
        if (FMessageDialog::Open(EAppMsgType::YesNo, WarningText) == EAppReturnType::No)
            return;
    }

    FString ResultAnimationName = SelectedAnimation->GetName() + "_rootmotion";
    UAnimSequence* ResultAnimation = DuplicateObject(SelectedAnimation, SelectedAnimation->GetOuter(), FName(*ResultAnimationName));

    if (ExecuteExtraction(ResultAnimation, SelectedInPlaceAnimation))
    {
        ResultAnimation->bEnableRootMotion = true;

        FAssetRegistryModule::AssetCreated(ResultAnimation);
        ResultAnimation->MarkPackageDirty();
        //ResultAnimation->GetOuter()->MarkPackageDirty();

        // focus the content browser on the new animation
        FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
        TArray<UObject*> SyncObjects;
        SyncObjects.Add(ResultAnimation);
        ContentBrowserModule.Get().SyncBrowserToAssets(SyncObjects);
    }
    else 
    {
        ResultAnimation->BeginDestroy();
    }
}



bool FMixamoAnimationRootMotionSolver::CanExecuteProcedure(const USkeleton* Skeleton) const
{
    // Check the asset content.
    // NOTE: this will load the asset if needed.
    if (!FMixamoAnimationRetargetingModule::Get().GetMixamoSkeletonRetargeter()->IsMixamoSkeleton(Skeleton))
    {
        return false;
    }

    // Check that the skeleton was processed with our retargeter !
    int32 RootBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(TEXT("root"));
    if (RootBoneIndex == INDEX_NONE)
    {
        return false;
    }

    return true;
}



bool FMixamoAnimationRootMotionSolver::ExecuteExtraction(UAnimSequence* AnimSequence, const UAnimSequence* InPlaceAnimSequence)
{
    // find the hips bone track index
    int32 HipsBoneIndex;
    bool HipsFound = AnimSequence->GetAnimationTrackNames().Find(FName("Hips"), HipsBoneIndex);
    //check(HipsFound);
    if (!HipsFound)
        return false;

    // take the hips bone track data from both animation sequences
    auto& RawAnimationData = const_cast<TArray<FRawAnimSequenceTrack>&>(AnimSequence->GetRawAnimationData());
    FRawAnimSequenceTrack& HipsBoneTrack = RawAnimationData[HipsBoneIndex];
    const FRawAnimSequenceTrack& InPlaceHipsBoneTrack = InPlaceAnimSequence->GetRawAnimationData()[HipsBoneIndex];

    // make a new track for the root bone
    // the keys num is equal to the hips keys num
    FRawAnimSequenceTrack RootBoneTrack;
    RootBoneTrack.PosKeys.SetNum(HipsBoneTrack.PosKeys.Num());
    RootBoneTrack.RotKeys.SetNum(HipsBoneTrack.RotKeys.Num());

    // HipsBoneTrack = Root + Hips
    // InPlaceHipsBoneTrack = Hips
    // we want to extract the Root value and set to the new root track so:
    // Root = HipsBoneTrack - InPlaceHipsBoneTrack = (Root + Hips) - Hips = Root
    for (int i = 0; i < HipsBoneTrack.PosKeys.Num(); ++i)
    {
        RootBoneTrack.PosKeys[i] = HipsBoneTrack.PosKeys[i] - InPlaceHipsBoneTrack.PosKeys[i];
        RootBoneTrack.RotKeys[i] = HipsBoneTrack.RotKeys[i] * InPlaceHipsBoneTrack.RotKeys[i].Inverse();
    }

    // now we can replace the HipsBoneTrack with InPlaceHipsBoneTrack
    HipsBoneTrack = InPlaceHipsBoneTrack;

    // take a copy of all tracks data & name
    // because we want to add the new root track as the first item
    // see https://bitbucket.org/unamedia/ue4-mixamo-plugin/issues/5/process-root-motion-is-not-correctly
    TArray<FName> TrackNamesCopy = AnimSequence->GetAnimationTrackNames();
    TArray<FRawAnimSequenceTrack> RawAnimationDataCopy = AnimSequence->GetRawAnimationData();

    // remove all tracks
    AnimSequence->RemoveAllTracks();

    // add the new root track (now as the first item)
    AnimSequence->AddNewRawTrack(FName("root"), &RootBoneTrack);

    // re-add all old other tracks
    check(TrackNamesCopy.Num() == RawAnimationDataCopy.Num());
    for (int i = 0; i < TrackNamesCopy.Num(); ++i)
    {
        AnimSequence->AddNewRawTrack(TrackNamesCopy[i], &RawAnimationDataCopy[i]);
    }

    // notify changes
    AnimSequence->PostProcessSequence();

    return true;
}



float FMixamoAnimationRootMotionSolver::GetMaxBoneDisplacement(const UAnimSequence* AnimSequence, const FName& BoneName)
{
    int32 HipsBoneIndex;
    bool HipsFound = AnimSequence->GetAnimationTrackNames().Find(BoneName, HipsBoneIndex);
    //check(HipsFound);
    if (!HipsFound)
        return 0;

    const FRawAnimSequenceTrack& HipsBoneTrack = AnimSequence->GetRawAnimationData()[HipsBoneIndex];

    float MaxSize = 0;

    for (int i = 0; i < HipsBoneTrack.PosKeys.Num(); ++i)
    {
        float Size = HipsBoneTrack.PosKeys[i].Size();
        if (Size > MaxSize)
            MaxSize = Size;
    }

    return MaxSize;
}



const UAnimSequence* FMixamoAnimationRootMotionSolver::EstimateInPlaceAnimation(const UAnimSequence* AnimationA, const UAnimSequence* AnimationB)
{
    FName RefBoneName(TEXT("Hips"));

    /*
    Find the "in place" animation sequence. To do that we compare the two hips bone displacements.
    The animation sequence with the lower value is the "in place" one.
    @TODO: is this checks always reliable ?
    */
    float dA = GetMaxBoneDisplacement(AnimationA, RefBoneName);
    float dB = GetMaxBoneDisplacement(AnimationB, RefBoneName);

    const UAnimSequence* NormalAnimSequence = (dA < dB) ? AnimationB : AnimationA;
    const UAnimSequence* InPlaceAnimSequence = (dA < dB) ? AnimationA : AnimationB;

    return InPlaceAnimSequence;
}
