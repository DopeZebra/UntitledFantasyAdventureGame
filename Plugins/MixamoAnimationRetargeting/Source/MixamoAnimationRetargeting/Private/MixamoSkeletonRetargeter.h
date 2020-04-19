// Copyright 2017 UNAmedia. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"



class URig;
class USkeleton;
class USkeletalMesh;
struct FReferenceSkeleton;



/**
Manage the retargeting of a Mixamo skeleton.

Further info:
- https://docs.unrealengine.com/latest/INT/Engine/Animation/Skeleton/
- https://docs.unrealengine.com/latest/INT/Engine/Animation/AnimationRetargeting/index.html
- https://docs.unrealengine.com/latest/INT/Engine/Animation/AnimHowTo/Retargeting/index.html
- https://docs.unrealengine.com/latest/INT/Engine/Animation/RetargetingDifferentSkeletons/
*/
class FMixamoSkeletonRetargeter
{
public:
	FMixamoSkeletonRetargeter();

	void RetargetToUE4Mannequin(TArray<USkeleton *> Skeletons) const;
	bool IsMixamoSkeleton(const USkeleton * Skeleton) const;

private:
	mutable TWeakObjectPtr<URig> EngineHumanoidRig;

private:
	void Retarget(USkeleton* Skeleton, const USkeleton * ReferenceSkeleton) const;
	void AddRootBone(USkeleton * Skeleton, TArray<USkeletalMesh *> SkeletalMeshes) const;
	void AddRootBone(const USkeleton * Skeleton, FReferenceSkeleton * RefSkeleton) const;
	bool HasFakeRootBone(const USkeleton* Skeleton) const;
	void SetupTranslationRetargetingModes(USkeleton* Skeleton) const;
	void SetupRigConfiguration(USkeleton* Skeleton, URig * HumanoidRig) const;
	void RetargetBasePose(TArray<USkeletalMesh *> SkeletalMeshes, const USkeleton * ReferenceSkeleton) const;
	USkeleton * AskUserForTargetSkeleton() const;
	/// Valid within a single method's stack space.
	URig * GetHumanoidRig() const;
	void GetAllSkeletalMeshesUsingSkeleton(const USkeleton * Skeleton, TArray<FAssetData> & SkeletalMeshes) const;
	void SetPreviewMesh(USkeleton * Skeleton, USkeletalMesh * PreviewMesh) const;
};
