// Copyright 2017 UNAmedia. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"



class USkeleton;
class USkeletalMesh;
struct FReferenceSkeleton;



/// To be used within a single method's stack space.
class FBoneMapper
{
public:
	FBoneMapper(const USkeleton * ASource, const USkeleton * ADestination)
		: Source(ASource),
		  Destination(ADestination)
	{}

	virtual
	~FBoneMapper()
	{}

	virtual
	int32 MapBoneIndex(int32 BoneIndex) const = 0;

protected:
	const USkeleton * Source;
	const USkeleton * Destination;
};



class FRigConfigurationBoneMapper : public FBoneMapper
{
public:
	FRigConfigurationBoneMapper(const USkeleton * ASource, const USkeleton * ADestination)
		: FBoneMapper(ASource, ADestination)
	{}

	virtual
	int32 MapBoneIndex(int32 BoneIndex) const override;
};



class FEqualNameBoneMapper : public FBoneMapper
{
public:
	FEqualNameBoneMapper(const USkeleton * ASource, const USkeleton * ADestination)
		: FBoneMapper(ASource, ADestination)
	{}

	virtual
	int32 MapBoneIndex(int32 BoneIndex) const override;
};



/// To be used within a single method's stack space.
class FSkeletonPoser
{
public:
	FSkeletonPoser(const USkeleton * Reference, const FReferenceSkeleton & RefSkeleton);
	void Pose(USkeletalMesh * Mesh, const FBoneMapper & BoneMapper, const TArray<FName> & PreserveCSBonesNames) const;

	// Utility methods.
	void PoseBasedOnRigConfiguration(USkeletalMesh * Mesh, const TArray<FName> & PreserveCSBonesNames) const;
	void PoseBasedOnCommonBoneNames(USkeletalMesh * Mesh, const TArray<FName> & PreserveCSBonesNames) const;

private:
	const USkeleton * ReferenceSkeleton;
	TArray<FTransform> ReferenceCSBonePoses;

private:
	void Pose(const FReferenceSkeleton & EditRefSkeleton, const FBoneMapper & BoneMapper, const TSet<int32> & PreserveCSBonesIndices, TArray<FTransform> & MeshBonePoses) const;

	static
	FTransform ComputeComponentSpaceTransform(const FReferenceSkeleton & RefSkeleton, const TArray<FTransform> & RelTransforms, int32 BoneIndex);
	static
	void BoneSpaceToComponentSpaceTransforms(const FReferenceSkeleton & RefSkeleton, const TArray<FTransform> & BSTransforms, TArray<FTransform> & CSTransforms);
	static
	void NumOfChildren(const FReferenceSkeleton & RefSkeleton, TArray<int> & children);

	static
	void LogReferenceSkeleton(const FReferenceSkeleton & RefSkeleton, const TArray<FTransform> & Poses, int BoneIndex = 0, int Deep = 0);
};
