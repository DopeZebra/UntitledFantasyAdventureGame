// Copyright 2017 UNAmedia. All Rights Reserved.

#include "MixamoSkeletonRetargeter.h"
#include "MixamoToolkitPrivatePCH.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"

#include "ARFilter.h"
#include "AssetRegistryModule.h"
#include "ScopedTransaction.h"
#include "Misc/ScopedSlowTask.h"

#include "SkeletonPoser.h"

#include "Editor.h"
#include "SMixamoToolkitWidget.h"
#include "ComponentReregisterContext.h"
#include "Components/SkinnedMeshComponent.h"



#define LOCTEXT_NAMESPACE "FMixamoAnimationRetargetingModule"



// Define it to disable the automatic addition of the Root Bone (needed to support UE4 Root Animations).
//#define MAR_ADDROOTBONE_DISABLE_

#ifdef MAR_ADDROOTBONE_DISABLE_
#	pragma message ("***WARNING*** Feature \"AddRootBone\" disabled.")
#endif



namespace
{



/**
Index of the last Mixamo bone, in UE4_Mixamo_BonesMapping array,
used to determine if a skeleton is from Mixamo.

Given the pair N-th, then index i = N * 2 - 1.
*/
static constexpr int IndexLastCheckedMixamoBone = 23 * 2 - 1;



/**
Mapping of UE4 "Humanoid" rig node names to Mixamo bone names.

NOTES:
- includes the added "Root" bone (by default it's missing in Mixamo skeletons).
- the first N pairs [ N = (IndexLastCheckedMixamoBone + 1) / 2 ] are used to
	determine if a skeleton is from Mixamo.
*/
static const char* UE4_Mixamo_BonesMapping[] = {
	// UE4 rig nodes		MIXAMO bones
	"Root",					"root",
	"Pelvis", 				"Hips",
	"spine_01", 			"Spine",
	"spine_02", 			"Spine1",
	"spine_03", 			"Spine2",
	"neck_01", 				"Neck",
	"head", 				"head",
	"clavicle_l", 			"LeftShoulder",
	"upperarm_l", 			"LeftArm",
	"lowerarm_l", 			"LeftForeArm",
	"hand_l", 				"LeftHand",
	"clavicle_r", 			"RightShoulder",
	"upperarm_r", 			"RightArm",
	"lowerarm_r", 			"RightForeArm",
	"hand_r", 				"RightHand",
	"thigh_l", 				"LeftUpLeg",
	"calf_l", 				"LeftLeg",
	"foot_l", 				"LeftFoot",
	"ball_l", 				"LeftToeBase",
	"thigh_r", 				"RightUpLeg",
	"calf_r",				"RightLeg",
	"foot_r", 				"RightFoot",
	"ball_r", 				"RightToeBase",
	// From here, ignored to determine if a skeleton is from Mixamo.
	"index_01_l", 			"LeftHandIndex1",
	"index_02_l",			"LeftHandIndex2",
	"index_03_l", 			"LeftHandIndex3",
	"middle_01_l", 			"LeftHandMiddle1",
	"middle_02_l", 			"LeftHandMiddle2",
	"middle_03_l", 			"LeftHandMiddle3",
	"pinky_01_l", 			"LeftHandPinky1",
	"pinky_02_l", 			"LeftHandPinky2",
	"pinky_03_l", 			"LeftHandPinky3",
	"ring_01_l", 			"LeftHandRing1",
	"ring_02_l", 			"LeftHandRing2",
	"ring_03_l", 			"LeftHandRing3",
	"thumb_01_l", 			"LeftHandThumb1",
	"thumb_02_l", 			"LeftHandThumb2",
	"thumb_03_l", 			"LeftHandThumb3",
	"index_01_r", 			"RightHandIndex1",
	"index_02_r", 			"RightHandIndex2",
	"index_03_r", 			"RightHandIndex3",
	"middle_01_r", 			"RightHandMiddle1",
	"middle_02_r", 			"RightHandMiddle2",
	"middle_03_r", 			"RightHandMiddle3",
	"pinky_01_r", 			"RightHandPinky1",
	"pinky_02_r", 			"RightHandPinky2",
	"pinky_03_r", 			"RightHandPinky3",
	"ring_01_r", 			"RightHandRing1",
	"ring_02_r", 			"RightHandRing2",
	"ring_03_r", 			"RightHandRing3",
	"thumb_01_r", 			"RightHandThumb1",
	"thumb_02_r", 			"RightHandThumb2",
	"thumb_03_r", 			"RightHandThumb3",
	// Un-mapped bones (at the moment). Here for reference.
	//"lowerarm_twist_01_l", 	nullptr,
	//"upperarm_twist_01_l", 	nullptr,
	//"lowerarm_twist_01_r", 	nullptr,
	//"upperarm_twist_01_r", 	nullptr,
	//"calf_twist_01_l", 		nullptr,
	//"thigh_twist_01_l", 	nullptr,
	//"calf_twist_01_r", 		nullptr,
	//"thigh_twist_01_r", 	nullptr,
	//"ik_foot_root",			nullptr,
	//"ik_foot_l",			nullptr,
	//"ik_foot_r",			nullptr,
	//"ik_hand_root",			nullptr,
	//"ik_hand_gun",			nullptr,
	//"ik_hand_l",			nullptr,
	//"ik_hand_r",			nullptr,
};



static_assert (IndexLastCheckedMixamoBone % 2 == 1, "Mixamo indexes are odd numbers");
static_assert (IndexLastCheckedMixamoBone >= 1, "First valid Mixamo index is 1");
static_assert (IndexLastCheckedMixamoBone < sizeof(UE4_Mixamo_BonesMapping) / sizeof(decltype(UE4_Mixamo_BonesMapping[0])), "Index out of bounds");



static const TArray<FName> Mixamo_PreserveComponentSpacePose_BoneNames = {
	"Head",
	"LeftToeBase",
	"RightToeBase"
};



static const FName RootBoneName("root");


} // namespace *unnamed*



FMixamoSkeletonRetargeter::FMixamoSkeletonRetargeter ()
	: EngineHumanoidRig(nullptr)
{
}



void FMixamoSkeletonRetargeter::RetargetToUE4Mannequin(TArray<USkeleton *> Skeletons) const
{
	if (Skeletons.Num() <= 0)
	{
		return;
	}

	// Get the UE4 "Mannequin" skeleton.
	USkeleton * ReferenceSkeleton = AskUserForTargetSkeleton();
	if (ReferenceSkeleton == nullptr)
	{
		// We hadn't found a suitable skeleton.
		UE_LOG(LogMixamoToolkit, Error, TEXT("No suitable Skeleton selected. Retargeting aborted."));
		return;
	}

	// Set the rig used by UE4 Mannequin skeleton, if needed.
	// It's used by the editor to detect the retargetable animations.
	if (ReferenceSkeleton->GetRig() == nullptr)
	{
		// "Humanoid.Humanoid" is the rig used by "UE4_Mannequin_Skeleton".
		URig* HumanoidRig = GetHumanoidRig();
		check(HumanoidRig != nullptr);
		const FScopedTransaction Transaction(LOCTEXT("FMixamoSkeletonRetargeter_FixMissingRigging", "Set rig configuration"));
		ReferenceSkeleton->Modify();
		ReferenceSkeleton->SetRigConfig(HumanoidRig);
		UE_LOG(LogMixamoToolkit, Warning, TEXT("Fixed missing rigging configuration in UE4 Mannequin skeleton '%s'"), *ReferenceSkeleton->GetName());
	}

	// Process all input skeletons.
	FScopedSlowTask Progress(Skeletons.Num(), LOCTEXT("FMixamoSkeletonRetargeter_ProgressTitle", "Retargeting of Mixamo assets"));
	Progress.MakeDialog();
	const FScopedTransaction Transaction(LOCTEXT("FMixamoSkeletonRetargeter_RetargetSkeletons", "Retargeting of Mixamo assets"));
	for (USkeleton * Skeleton : Skeletons)
	{
		Progress.EnterProgressFrame(1, FText::FromName(Skeleton->GetFName()));
		Retarget(Skeleton, ReferenceSkeleton);
	}
}



bool FMixamoSkeletonRetargeter::IsMixamoSkeleton(const USkeleton * Skeleton) const
{
	// We consider a Skeleton "coming from Mixamo" if it has at least X% of the expected bones.
	const float MINIMUM_MATCHING_PERCENTAGE = .75f;

	// No Skeleton, No Mixamo...
	if (Skeleton == nullptr)
	{
		return false;
	}

	// Look for and count the known Mixamo bones (see comments on IndexLastCheckedMixamoBone and UE4_Mixamo_BonesMapping).
	const int32 NumExpectedBones = (IndexLastCheckedMixamoBone + 1) / 2;
	int32 nMatchingBones = 0;
	const FReferenceSkeleton & SkeletonRefSkeleton = Skeleton->GetReferenceSkeleton();
	for (int i = 0; i < NumExpectedBones; ++i)
	{
		check(UE4_Mixamo_BonesMapping[i * 2 + 1] != nullptr);
		const FName & MixamoBoneName = UE4_Mixamo_BonesMapping[i * 2 + 1];
		const int32 MixamoBoneIndex = SkeletonRefSkeleton.FindBoneIndex(MixamoBoneName);
		if (MixamoBoneIndex != INDEX_NONE)
		{
			++nMatchingBones;
		}
	}
	const float MatchedPercentage = float(nMatchingBones) / float(NumExpectedBones);

	return MatchedPercentage >= MINIMUM_MATCHING_PERCENTAGE;
}



void FMixamoSkeletonRetargeter::Retarget(USkeleton* Skeleton, const USkeleton * ReferenceSkeleton) const
{
	check(Skeleton != nullptr);
	check(ReferenceSkeleton != nullptr);
	check(ReferenceSkeleton->GetRig() != nullptr);

	UE_LOG(LogMixamoToolkit, Log, TEXT("Retargeting Mixamo skeleton '%s'"), *Skeleton->GetName());

	// Check for a skeleton retargeting on itself.
	if (Skeleton == ReferenceSkeleton)
	{
		UE_LOG(LogMixamoToolkit, Warning, TEXT("Skipping retargeting of Mixamo skeleton '%s' on itself"), *Skeleton->GetName());
		return;
	}

	// Check for invalid root bone (root bone not at position 0)
	if (HasFakeRootBone(Skeleton))
	{
		UE_LOG(LogMixamoToolkit, Warning, TEXT("Skipping retargeting of Mixamo skeleton '%s'; invalid 'root' bone at index != 0"), *Skeleton->GetName());
		return;
	}

	// Get all USkeletalMesh assets using Skeleton.
	TArray<FAssetData> Assets;
	GetAllSkeletalMeshesUsingSkeleton(Skeleton, Assets);
	TArray<USkeletalMesh *> SkeletalMeshes;
	SkeletalMeshes.Reserve(Assets.Num());
	for (FAssetData & Asset : Assets)
	{
		SkeletalMeshes.Add(CastChecked<USkeletalMesh> (Asset.GetAsset()));
	}

	/*
		Retargeting uses the SkeletalMesh's reference skeleton, as it counts for mesh proportions.
		If you need to use the original Skeleton, you have to ensure Skeleton pose has the same proportions
		of the skeletal mesh we are retargeting calling:
			Skeleton->GetPreviewMesh(true)->UpdateReferencePoseFromMesh(SkeletonMesh);
	*/
	// Add the root bone if needed. This: fixes a offset glitch in the animations, is generally useful.
#ifndef MAR_ADDROOTBONE_DISABLE_
	AddRootBone(Skeleton, SkeletalMeshes);
#endif
	// Set the same rig configuration used by UE4 "UE4_Mannequin_Skeleton".
	SetupRigConfiguration(Skeleton, ReferenceSkeleton->GetRig());
	// Set-up the translation retargeting modes, to avoid artifacts when retargeting the animations.
	SetupTranslationRetargetingModes(Skeleton);
	// Retarget the base pose so that Skeleton and "UE4_Mannequin_Skeleton" have the same pose.
	RetargetBasePose(SkeletalMeshes, ReferenceSkeleton);

	// Be sure that the Skeleton has a preview mesh!
	// without it, retargeting an animation will fail
	if (SkeletalMeshes.Num() > 0)
		SetPreviewMesh(Skeleton, SkeletalMeshes[0]);
}



bool FMixamoSkeletonRetargeter::HasFakeRootBone(const USkeleton* Skeleton) const
{
	check(Skeleton != nullptr);

	// returns true if the skeleton has a bone named 'root' but not a position ZERO, false otherwise 

	const int32 rootBoneIndex = Skeleton->GetReferenceSkeleton().FindBoneIndex(RootBoneName);
	return rootBoneIndex != INDEX_NONE && rootBoneIndex != 0;
}



void FMixamoSkeletonRetargeter::AddRootBone(USkeleton* Skeleton, TArray<USkeletalMesh *> SkeletalMeshes) const
{
	// Skip if the mesh has already a bone named "root".
	if (Skeleton->GetReferenceSkeleton().FindBoneIndex(RootBoneName) != INDEX_NONE)
	{
		return;
	}

	//=== Add the root bone to all the Skeletal Meshes using Skeleton.
	// We'll have to fix the Skeletal Meshes to get rid of the added root bone.

	// When going out of scope, it'll re-register components with the scene.
	TComponentReregisterContext<USkinnedMeshComponent> ReregisterContext;

	// Add the root bone to *all* skeletal meshes in SkeletalMeshes.
	for (int iMesh = 0; iMesh < SkeletalMeshes.Num(); ++ iMesh)
	{
		USkeletalMesh * SkeletonMesh = SkeletalMeshes[iMesh];
		ensure(SkeletonMesh != nullptr);	// @TODO: manage the nullptr case.
		check(SkeletonMesh->Skeleton == Skeleton);

		SkeletonMesh->Modify();

		SkeletonMesh->ReleaseResources();
		SkeletonMesh->ReleaseResourcesFence.Wait();

		// Add the root bone to the skeletal mesh's reference skeleton.
		AddRootBone(SkeletonMesh->Skeleton, &SkeletonMesh->RefSkeleton);
		// Fix-up bone transforms and reset RetargetBasePose.
		SkeletonMesh->RetargetBasePose.Empty();
		SkeletonMesh->CalculateInvRefMatrices();	// @BUG: UE4 Undo system fails to undo the CalculateInvRefMatrices() effect.

		// As we added a new parent bone, fix "old" Skeletal Mesh indices.
		for (FSkeletalMeshLODModel & LODModel : SkeletonMesh->GetImportedModel()->LODModels)
		{
			// == Fix the list of bones used by LODModel.

			// Increase old ActiveBoneIndices by 1, to compensate the new root bone.
			for (FBoneIndexType & i : LODModel.ActiveBoneIndices)
			{
				++i;
			}
			// Add the new root bone to the ActiveBoneIndices.
			LODModel.ActiveBoneIndices.Insert(0, 0);

			// Increase old RequiredBones by 1, to compensate the new root bone.
			for (FBoneIndexType & i : LODModel.RequiredBones)
			{
				++i;
			}
			// Add the new root bone to the RequiredBones.
			LODModel.RequiredBones.Insert(0, 0);

			// Updated the bone references used by the SkinWeightProfiles
			for (auto & Kvp : LODModel.SkinWeightProfiles)
			{
				FImportedSkinWeightProfileData & SkinWeightProfile = LODModel.SkinWeightProfiles.FindChecked(Kvp.Key);

				// Increase old InfluenceBones by 1, to compensate the new root bone.
				for (FRawSkinWeight & w : SkinWeightProfile.SkinWeights)
				{
					for (int i = 0; i < MAX_TOTAL_INFLUENCES; ++ i)
					{
						if (w.InfluenceWeights[i] > 0)
						{
							++ w.InfluenceBones[i];
						}
					}
				}

				// Increase old BoneIndex by 1, to compensate the new root bone.
				for (SkeletalMeshImportData::FVertInfluence & v: SkinWeightProfile.SourceModelInfluences)
				{
					if (v.Weight > 0)
					{
						++ v.BoneIndex;
					}
				}
			}

			// == Fix the mesh LOD sections.
			FRawSkeletalMeshBulkData & LODRawBulkData = LODModel.RawSkeletalMeshBulkData;

			// Since UE4.24, newly imported Skeletal Mesh asset (UASSET) are serialized with additional data
			// and are processed differently. On the post-edit change of the asset, the editor automatically
			// re-builds all the sections starting from the stored raw mesh, if available.
			// This is made to properly re-apply the reduction settings after changes.
			// In this case, we must update the bones in the raw mesh and the editor will rebuild LODModel.Sections.
			if (LODRawBulkData.IsBuildDataAvailable () && !LODRawBulkData.IsEmpty())
			{
				FSkeletalMeshImportData RawMesh;
				LODModel.RawSkeletalMeshBulkData.LoadRawMesh(RawMesh);

				// Increase old ParentIndex by 1, to compensate the new root bone.
				int32 NumRootChildren = 0;
				for (SkeletalMeshImportData::FBone & b : RawMesh.RefBonesBinary)
				{
					if (b.ParentIndex == INDEX_NONE)
					{
						NumRootChildren += b.NumChildren;
					}
					++ b.ParentIndex;
				}
				// Add the new root bone to the RefBonesBinary.
				check(NumRootChildren > 0);
				const SkeletalMeshImportData::FJointPos NewRootPos = { FTransform::Identity, 1.f, 100.f, 100.f, 100.f };
				const SkeletalMeshImportData::FBone NewRoot = { RootBoneName.ToString(), 0, NumRootChildren, INDEX_NONE, NewRootPos };
				RawMesh.RefBonesBinary.Insert(NewRoot, 0);

				// Increase old BoneIndex by 1, to compensate the new root bone.
				// Influences stores the pairs (vertex, bone), no need to add new items.
				for (SkeletalMeshImportData::FRawBoneInfluence & b : RawMesh.Influences)
				{
					++ b.BoneIndex;
				}

				if (RawMesh.MorphTargets.Num() > 0)
				{
					UE_LOG(LogMixamoToolkit, Warning, TEXT("MorphTargets are not supported."));
				}

				if (RawMesh.AlternateInfluences.Num() > 0)
				{
					UE_LOG(LogMixamoToolkit, Warning, TEXT("AlternateInfluences are not supported."));
				}

				LODModel.RawSkeletalMeshBulkData.SaveRawMesh(RawMesh);
			}
			else
			{
				// For Skeletal Mesh assets (UASSET) using a pre-UE4.24 format (or missing the raw mesh data),
				// we must manually update the LODModel.Sections to keep them synchronized with the new added root bone.
				for (FSkelMeshSection & LODSection : LODModel.Sections)
				{
					// Increase old BoneMap indices by 1, to compensate the new root bone.
					for (FBoneIndexType & i : LODSection.BoneMap)
					{
						++i;
					}
					// No need to add the new root bone to BoneMap, as no vertices would use it.
					//
					// No need to update LODSection.SoftVertices[] items as FSoftSkinVertex::InfluenceBones
					// contains indices over LODSection.BoneMap, that does't changed items positions.
				}
			}
		}

		SkeletonMesh->PostEditChange();
		SkeletonMesh->InitResources();

		// Use the modified skeletal mesh to recreate the skeleton bones structure, so it'll contains also the new root bone.
		// NOTE: this would invalidate the animations.
		Skeleton->Modify();
		if (iMesh == 0)
		{
			Skeleton->RecreateBoneTree(SkeletonMesh);
		}
		else
		{
			Skeleton->MergeAllBonesToBoneTree(SkeletonMesh);
		}
	}
}



void FMixamoSkeletonRetargeter::AddRootBone(const USkeleton * Skeleton, FReferenceSkeleton * RefSkeleton) const
{
	check(Skeleton != nullptr);
	check(RefSkeleton != nullptr);
	checkf(RefSkeleton->FindBoneIndex(RootBoneName) == INDEX_NONE, TEXT("The reference skeleton has already a \"root\" bone."));

	//=== Create a new FReferenceSkeleton with the root bone added.
	FReferenceSkeleton NewRefSkeleton;
	{
		// Destructor rebuilds the ref-skeleton.
		FReferenceSkeletonModifier RefSkeletonModifier(NewRefSkeleton, Skeleton);

		// Add the new root bone.
		const FMeshBoneInfo Root(RootBoneName, RootBoneName.ToString(), INDEX_NONE);
		RefSkeletonModifier.Add(Root, FTransform::Identity);

		// Copy and update existing bones indexes to get rid of the added root bone.
		for (int32 i = 0; i < RefSkeleton->GetRawBoneNum(); ++i)
		{
			FMeshBoneInfo info = RefSkeleton->GetRawRefBoneInfo()[i];
			info.ParentIndex += 1;
			const FTransform & pose = RefSkeleton->GetRawRefBonePose()[i];
			RefSkeletonModifier.Add(info, pose);
		}
	}

	// Set the new Reference Skeleton.
	*RefSkeleton = NewRefSkeleton;
}



// See:
//	- https://docs.unrealengine.com/latest/INT/Engine/Animation/AnimationRetargeting/index.html#settingupretargeting
//	- https://docs.unrealengine.com/latest/INT/Engine/Animation/RetargetingDifferentSkeletons/#retargetingadjustments
//	- https://docs.unrealengine.com/latest/INT/Engine/Animation/AnimHowTo/Retargeting/index.html#retargetingusingthesameskeleton
void FMixamoSkeletonRetargeter::SetupTranslationRetargetingModes(USkeleton* Skeleton) const
{
	check(Skeleton != nullptr);

	const FReferenceSkeleton & RefSkeleton = Skeleton->GetReferenceSkeleton();
	Skeleton->Modify();

	// Convert all bones, starting from the root one, to "Skeleton".
	// This will ensure that all bones use the skeleton's static translation.
	const int32 RootIndex = 0;
#ifndef MAR_ADDROOTBONE_DISABLE_
	checkf(RefSkeleton.FindBoneIndex(RootBoneName) == RootIndex, TEXT("Root bone at index 0"));
#endif
	Skeleton->SetBoneTranslationRetargetingMode(RootIndex, EBoneTranslationRetargetingMode::Skeleton, true);
	// Set the Pelvis bone (in Mixamo it's called "Hips") to AnimationScaled.
	// This will make sure that the bone sits at the right height and is still animated.
	const int32 PelvisIndex = RefSkeleton.FindBoneIndex(TEXT("Hips"));
	if (PelvisIndex != INDEX_NONE)
	{
		Skeleton->SetBoneTranslationRetargetingMode(PelvisIndex, EBoneTranslationRetargetingMode::AnimationScaled);
	}
	// Find the Root bone, any IK bones, any Weapon bones you may be using or other marker-style bones and set them to Animation.
	// This will make sure that bone's translation comes from the animation data itself and is unchanged.
	// @TODO: do it for IK bones.
	Skeleton->SetBoneTranslationRetargetingMode(RootIndex, EBoneTranslationRetargetingMode::Animation);
}



void FMixamoSkeletonRetargeter::SetupRigConfiguration(USkeleton* Skeleton, URig * HumanoidRig) const
{
	check(Skeleton != nullptr);
	check(HumanoidRig != nullptr);

	// Set the same rig configuration used by UE4 "UE4_Mannequin_Skeleton".
	Skeleton->Modify();
	Skeleton->SetRigConfig(HumanoidRig);

	// Map the bones names: rig node name ("Humanoid.Humanoid") to bone name (Mixamo names).
	for (int i = 0; i < sizeof(UE4_Mixamo_BonesMapping) / sizeof(char*); i += 2)
	{
		check(UE4_Mixamo_BonesMapping[i] != nullptr);
		check(UE4_Mixamo_BonesMapping[i + 1] != nullptr);
		Skeleton->SetRigBoneMapping(UE4_Mixamo_BonesMapping[i], UE4_Mixamo_BonesMapping[i + 1]);
	}
}



void FMixamoSkeletonRetargeter::RetargetBasePose(TArray<USkeletalMesh *> SkeletalMeshes, const USkeleton * ReferenceSkeleton) const
{
	check(ReferenceSkeleton != nullptr);

	// @NOTE: UE4 mannequin skeleton must have same pose & proportions as of its skeletal mesh.
	FSkeletonPoser poser(ReferenceSkeleton, ReferenceSkeleton->GetReferenceSkeleton());

	// Retarget all Skeletal Meshes using Skeleton.
	for (USkeletalMesh * Mesh : SkeletalMeshes)
	{
		// Some of Mixamo's bones need a different rotation respect to UE4 mannequin reference pose.
		// An analytics solution would be preferred, but (for now) preserving the CS pose of their
		// children bones works quite well.
		poser.PoseBasedOnRigConfiguration(Mesh, Mixamo_PreserveComponentSpacePose_BoneNames);

		//@todo Add IK bones if possible.
	}
}



URig * FMixamoSkeletonRetargeter::GetHumanoidRig() const
{
	// Return cached instance, if any.
	if (EngineHumanoidRig.IsValid())
	{
		return EngineHumanoidRig.Get();
	}

	// "Humanoid.Humanoid" is the rig used by "UE4_Mannequin_Skeleton".
	EngineHumanoidRig = LoadObject<URig>(nullptr, TEXT("/Engine/EngineMeshes/Humanoid.Humanoid"), nullptr, LOAD_None, nullptr);
	ensureMsgf(EngineHumanoidRig.IsValid(), TEXT("\"Humanoid\" rig not found"));
	return EngineHumanoidRig.Get();
}



USkeleton * FMixamoSkeletonRetargeter::AskUserForTargetSkeleton() const
{
	TSharedRef<SWindow> WidgetWindow = SNew(SWindow)
		.Title(LOCTEXT("FMixamoSkeletonRetargeter_AskUserForTargetSkeleton_WindowTitle", "Select retargeting skeleton"))
		.ClientSize(FVector2D(500, 600))
		.SupportsMinimize(false)
		.SupportsMaximize(false)
		.HasCloseButton(false);

	TSharedRef<SRiggedSkeletonPicker> RiggedSkeletonPicker = SNew(SRiggedSkeletonPicker)
		.ReferenceRig(GetHumanoidRig())
		.Title(LOCTEXT("FMixamoSkeletonRetargeter_AskUserForTargetSkeleton_Title", "Select a Skeleton asset to use as retarget source."))
		.Description(LOCTEXT("FMixamoSkeletonRetargeter_AskUserForTargetSkeleton_Description", "It must have the Humanoid rig assigned. For optimal results, it should be the standard Unreal Engine 4 mannequin skeleton."));

	WidgetWindow->SetContent(RiggedSkeletonPicker);
	GEditor->EditorAddModalWindow(WidgetWindow);

	return RiggedSkeletonPicker->GetSelectedSkeleton();
}



void FMixamoSkeletonRetargeter::GetAllSkeletalMeshesUsingSkeleton(const USkeleton * Skeleton, TArray<FAssetData> & SkeletalMeshes) const
{
	SkeletalMeshes.Empty();

	FARFilter Filter;
	Filter.ClassNames.Add(USkeletalMesh::StaticClass()->GetFName());
	Filter.bRecursiveClasses = true;
	const FString SkeletonString = FAssetData(Skeleton).GetExportTextName();
	Filter.TagsAndValues.Add(GET_MEMBER_NAME_CHECKED(USkeletalMesh, Skeleton), SkeletonString);
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	AssetRegistryModule.Get().GetAssets(Filter, SkeletalMeshes);
}



void FMixamoSkeletonRetargeter::SetPreviewMesh(USkeleton * Skeleton, USkeletalMesh * PreviewMesh) const
{
	check(Skeleton != nullptr);

	if (Skeleton->GetPreviewMesh() == nullptr && PreviewMesh != nullptr)
		Skeleton->SetPreviewMesh(PreviewMesh);
}



#undef LOCTEXT_NAMESPACE
