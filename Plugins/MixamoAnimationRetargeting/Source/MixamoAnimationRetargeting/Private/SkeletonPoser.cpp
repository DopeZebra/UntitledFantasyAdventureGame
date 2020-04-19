// Copyright 2017 UNAmedia. All Rights Reserved.

#include "SkeletonPoser.h"
#include "MixamoToolkitPrivatePCH.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/Rig.h"



#define LOCTEXT_NAMESPACE "FMixamoAnimationRetargetingModule"



int32 FRigConfigurationBoneMapper::MapBoneIndex(int32 BoneIndex) const
{
	const FName SourceBoneName = Source->GetReferenceSkeleton().GetBoneName(BoneIndex);
	const FName RigNodeName = Source->GetRigNodeNameFromBoneName(SourceBoneName);
	const FName DestinationBoneName = Destination->GetRigBoneMapping(RigNodeName);
	const int32 DestinationBoneIndex = Destination->GetReferenceSkeleton().FindBoneIndex(DestinationBoneName);
	return DestinationBoneIndex;
}



int32 FEqualNameBoneMapper::MapBoneIndex(int32 BoneIndex) const
{
	const FName SourceBoneName = Source->GetReferenceSkeleton().GetBoneName(BoneIndex);
	const int32 DestinationBoneIndex = Destination->GetReferenceSkeleton().FindBoneIndex(SourceBoneName);
	return DestinationBoneIndex;
}



FSkeletonPoser::FSkeletonPoser(const USkeleton * Reference, const FReferenceSkeleton & RefSkeleton)
	: ReferenceSkeleton(Reference)
{
	check(ReferenceSkeleton != nullptr);
	BoneSpaceToComponentSpaceTransforms(RefSkeleton, RefSkeleton.GetRefBonePose(), ReferenceCSBonePoses);
}



void FSkeletonPoser::Pose(USkeletalMesh * Mesh, const FBoneMapper & BoneMapper, const TArray<FName> & PreserveCSBonesNames) const
{
	check(Mesh != nullptr);

	// Convert bone names to bone indices.
	TSet<int32> PreserveCSBonesIndices;
	if (PreserveCSBonesNames.Num() > 0)
	{
		PreserveCSBonesIndices.Reserve(PreserveCSBonesNames.Num());
		for (const FName & BoneName : PreserveCSBonesNames)
		{
			const int32 BoneIndex = Mesh->RefSkeleton.FindBoneIndex(BoneName);
			if (BoneIndex != INDEX_NONE)
			{
				PreserveCSBonesIndices.Add(BoneIndex);
			}
		}
	}

	UE_LOG(LogMixamoToolkit, Log, TEXT("BEGIN: %s -> %s"), *Mesh->GetName(), *ReferenceSkeleton->GetName());
	// We'll change RetargetBasePose below.
	Mesh->Modify();
	// NOTE: the RefSkeleton of the Skeletal Mesh counts for its mesh proportions.
	Pose(Mesh->RefSkeleton, BoneMapper, PreserveCSBonesIndices, Mesh->RetargetBasePose);
	UE_LOG(LogMixamoToolkit, Log, TEXT("END: %s -> %s"), *Mesh->GetName(), *ReferenceSkeleton->GetName());
}



void FSkeletonPoser::PoseBasedOnRigConfiguration(USkeletalMesh * Mesh, const TArray<FName> & PreserveCSBonesNames) const
{
	check(Mesh != nullptr);
	Pose(Mesh, FRigConfigurationBoneMapper(Mesh->Skeleton, ReferenceSkeleton), PreserveCSBonesNames);
}



void FSkeletonPoser::PoseBasedOnCommonBoneNames(USkeletalMesh * Mesh, const TArray<FName> & PreserveCSBonesNames) const
{
	check(Mesh != nullptr);
	Pose(Mesh, FEqualNameBoneMapper(Mesh->Skeleton, ReferenceSkeleton), PreserveCSBonesNames);
}



void FSkeletonPoser::Pose(
	const FReferenceSkeleton & EditRefSkeleton,
	const FBoneMapper & BoneMapper,
	const TSet<int32> & PreserveCSBonesIndices,
	TArray<FTransform> & EditBonePoses
) const
{
	check(ReferenceSkeleton != nullptr);
	// NOTE: ReferenceSkeleton is used only to get hierarchical infos.
	const FReferenceSkeleton & ReferenceRefSkeleton = ReferenceSkeleton->GetReferenceSkeleton();

	const int32 NumBones = EditRefSkeleton.GetNum();
	EditBonePoses = EditRefSkeleton.GetRefBonePose();
	check(EditBonePoses.Num() == NumBones);

	TArray<int> EditChildrens;
	NumOfChildren(EditRefSkeleton, EditChildrens);
	TArray<FTransform> OriginalEditCSBonePoses;
	if (PreserveCSBonesIndices.Num () > 0)
	{
		BoneSpaceToComponentSpaceTransforms(EditRefSkeleton, EditRefSkeleton.GetRefBonePose(), OriginalEditCSBonePoses);
	}

	//LogReferenceSkeleton(EditRefSkeleton, EditRefSkeleton.GetRefBonePose());
	for (int32 EditBoneIndex = 0; EditBoneIndex < NumBones; ++EditBoneIndex)
	{
		FVector ReferenceCSBoneOrientation;
		if (PreserveCSBonesIndices.Contains(EditBoneIndex))
		{
			// Compute orientation of reference bone, considering the original CS bone poses as reference.
			const int32 ReferenceBoneParentIndex = EditRefSkeleton.GetParentIndex(EditBoneIndex);
			check(ReferenceBoneParentIndex < EditBoneIndex && "Parent bone must have lower index");
			const FTransform & ReferenceCSParentTransform = (ReferenceBoneParentIndex != INDEX_NONE ? OriginalEditCSBonePoses[ReferenceBoneParentIndex] : FTransform::Identity);
			const FTransform & ReferenceCSTransform = OriginalEditCSBonePoses[EditBoneIndex];
			ReferenceCSBoneOrientation = (ReferenceCSTransform.GetLocation() - ReferenceCSParentTransform.GetLocation()).GetSafeNormal();
		}
		else
		{
			// Get the retarget bone on the reference skeleton.
			const int32 ReferenceBoneIndex = BoneMapper.MapBoneIndex(EditBoneIndex);
			if (ReferenceBoneIndex == INDEX_NONE)
			{
				// Bone not retargeted, skip.
				continue;
			}

			// Compute orientation of reference bone.
			const int32 ReferenceBoneParentIndex = ReferenceRefSkeleton.GetParentIndex(ReferenceBoneIndex);
			check(ReferenceBoneParentIndex < ReferenceBoneIndex && "Parent bone must have lower index");
			const FTransform & ReferenceCSParentTransform = (ReferenceBoneParentIndex != INDEX_NONE ? ReferenceCSBonePoses[ReferenceBoneParentIndex] : FTransform::Identity);
			const FTransform & ReferenceCSTransform = ReferenceCSBonePoses[ReferenceBoneIndex];
			ReferenceCSBoneOrientation = (ReferenceCSTransform.GetLocation() - ReferenceCSParentTransform.GetLocation()).GetSafeNormal();
			// Skip degenerated bones.
			if (ReferenceCSBoneOrientation.IsNearlyZero())
			{
				continue;
			}
		}

		// Compute current orientation of the bone to retarget (skeleton).
		const int32 EditBoneParentIndex = EditRefSkeleton.GetParentIndex(EditBoneIndex);
		check(EditBoneParentIndex < EditBoneIndex && "Parent bone must have been already retargeted");
		if (EditBoneParentIndex == INDEX_NONE)
		{
			// We must rotate the parent bone, but it doesn't exist. Skip.
			continue;
		}
		if (EditChildrens[EditBoneParentIndex] > 1)
		{
			// If parent bone has multiple children, modifying it here would ruin the sibling bones. Skip. [NOTE: this bone will differ from the expected result!]
			continue;
		}
		// Compute the transforms on the up-to-date skeleton (they cant' be cached).
		const FTransform SkeletonCSParentTransform = ComputeComponentSpaceTransform(EditRefSkeleton, EditBonePoses, EditBoneParentIndex);
		const FTransform SkeletonCSTransform = EditBonePoses[EditBoneIndex] * SkeletonCSParentTransform;
		const FVector SkeletonCSBoneOrientation = (SkeletonCSTransform.GetLocation() - SkeletonCSParentTransform.GetLocation()).GetSafeNormal();

		// Skip degenerated or already-aligned bones.
		if (SkeletonCSBoneOrientation.IsNearlyZero() || ReferenceCSBoneOrientation.Equals(SkeletonCSBoneOrientation))
		{
			continue;
		}

		// Delta rotation (in Component Space) to make the skeleton bone aligned to the reference one.
		const FQuat SkeletonToUE4CSRotation = FQuat::FindBetweenVectors(SkeletonCSBoneOrientation, ReferenceCSBoneOrientation);
		// Convert from Component Space to skeleton Bone Space
		const FQuat SkeletonToUE4BSRotationRot = SkeletonCSParentTransform.GetRotation().Inverse() * SkeletonToUE4CSRotation * SkeletonCSParentTransform.GetRotation();
		// Apply the rotation to the *parent* bone (yep!!!)
		EditBonePoses[EditBoneParentIndex].ConcatenateRotation(SkeletonToUE4BSRotationRot);
	}
}



FTransform FSkeletonPoser::ComputeComponentSpaceTransform(const FReferenceSkeleton & RefSkeleton, const TArray<FTransform> & RelTransforms, int32 BoneIndex)
{
	if (BoneIndex == INDEX_NONE)
	{
		return FTransform::Identity;
	}

	FTransform T = RelTransforms[BoneIndex];
	int32 i = RefSkeleton.GetParentIndex(BoneIndex);
	while (i != INDEX_NONE)
	{
		checkf(i < BoneIndex, TEXT("Parent bone must have been already retargeted"));
		T *= RelTransforms[i];
		i = RefSkeleton.GetParentIndex(i);
	}

	return T;
}



void FSkeletonPoser::BoneSpaceToComponentSpaceTransforms(const FReferenceSkeleton & RefSkeleton, const TArray<FTransform> & BSTransforms, TArray<FTransform> & CSTransforms)
{
	check(RefSkeleton.GetNum() == BSTransforms.Num());
	const int32 NumBones = RefSkeleton.GetNum();
	CSTransforms.Empty(NumBones);
	CSTransforms.AddUninitialized(NumBones);
	for (int32 iBone = 0; iBone < NumBones; ++iBone)
	{
		CSTransforms[iBone] = BSTransforms[iBone];
		const int32 iParent = RefSkeleton.GetParentIndex(iBone);
		check(iParent < iBone);
		if (iParent != INDEX_NONE)
		{
			CSTransforms[iBone] *= CSTransforms[iParent];
		}
	}
}



void FSkeletonPoser::NumOfChildren(const FReferenceSkeleton & RefSkeleton, TArray<int> & children)
{
	const int32 NumBones = RefSkeleton.GetNum();
	children.Empty(NumBones);
	children.AddUninitialized(NumBones);
	for (int32 iBone = 0; iBone < NumBones; ++iBone)
	{
		children[iBone] = 0;
		const int32 iParent = RefSkeleton.GetParentIndex(iBone);
		check(iParent < iBone);
		if (iParent != INDEX_NONE)
		{
			++children[iParent];
		}
	}
}



void FSkeletonPoser::LogReferenceSkeleton (const FReferenceSkeleton & RefSkeleton, const TArray<FTransform> & Poses, int BoneIndex, int Deep)
{
	FString Indent;
	for (int i = 0; i < Deep; ++i)
	{
		Indent.Append(TEXT("  "));
	}

	UE_LOG(LogMixamoToolkit, Log, TEXT("%s[%d - %s]: %s"), * Indent, BoneIndex, * RefSkeleton.GetBoneName(BoneIndex).ToString(), * Poses[BoneIndex].ToString ());

	for (int i = BoneIndex + 1; i < Poses.Num(); ++i)
	{
		if (RefSkeleton.GetParentIndex(i) == BoneIndex)
		{
			LogReferenceSkeleton(RefSkeleton, Poses, i, Deep + 1);
		}
	}
}



#undef LOCTEXT_NAMESPACE
