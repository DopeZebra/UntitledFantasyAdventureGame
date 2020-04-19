// Fill out your copyright notice in the Description page of Project Settings.


#include "MASFunctionLibrary.h"

#include "MirrorTable.h"

#include "Animation/AnimSequence.h"

#include "Misc/PackageName.h"

#if WITH_EDITOR
#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"
#include "Toolkits/AssetEditorManager.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#endif

#define LOCTEXT_NAMESPACE "MASLibrary"

UMASFunctionLibrary::UMASFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UMASFunctionLibrary::BulkMirrorEditorOnly(const TArray <UAnimSequence*> SourceAnims, const UMirrorTable* MirrorTable, TArray <UAnimSequence*>& OutNewAnims)
{
	if (SourceAnims.Num() == 0) return;
	if (MirrorTable == NULL) return;

#if WITH_EDITOR
	FAssetToolsModule& AssetToolsModule = FModuleManager::Get().LoadModuleChecked<FAssetToolsModule>("AssetTools");

	for (int32 i = 0; i < SourceAnims.Num(); i++)
	{
		// Create the asset
		FString Name;
		FString PackageName;


		FString Suffix = TEXT("_Mirrored");

		AssetToolsModule.Get().CreateUniqueAssetName(SourceAnims[i]->GetOutermost()->GetName(), Suffix, /*out*/ PackageName, /*out*/ Name);
		const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);


		UObject* NewAsset = AssetToolsModule.Get().DuplicateAsset(Name, PackagePath, SourceAnims[i]);

		if (NewAsset != NULL)
		{
			UAnimSequence* MirrorAnimSequence = Cast<UAnimSequence>(NewAsset);
			CreateMirrorSequenceFromAnimSequence(MirrorAnimSequence, MirrorTable);

			OutNewAnims.Add(MirrorAnimSequence);

			// Notify asset registry of new asset
			FAssetRegistryModule::AssetCreated(MirrorAnimSequence);

			// Display notification so users can quickly access
			if (GIsEditor)
			{
				FNotificationInfo Info(FText::Format(LOCTEXT("AnimationMirrored", "Successfully Mirrored Animation"), FText::FromString(MirrorAnimSequence->GetName())));
				Info.ExpireDuration = 8.0f;
				Info.bUseLargeFont = false;
				Info.Hyperlink = FSimpleDelegate::CreateLambda([=]() { FAssetEditorManager::Get().OpenEditorForAssets(TArray<UObject*>({ MirrorAnimSequence })); });
				Info.HyperlinkText = FText::Format(LOCTEXT("OpenNewAnimationHyperlink", "Open {0}"), FText::FromString(MirrorAnimSequence->GetName()));
				TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
				if (Notification.IsValid())
				{
					Notification->SetCompletionState(SNotificationItem::CS_Success);
				}
			}
		}
	}
#endif
}

#if WITH_EDITOR
void UMASFunctionLibrary::CreateMirrorSequenceFromAnimSequence(UAnimSequence* MirrorSequence, const UMirrorTable* MirrorTable)
{
	//Check if it's valid
	if ((MirrorSequence != NULL) && (MirrorTable != NULL) && (MirrorSequence->GetSkeleton() != NULL))
	{
		//Make the duplicate that I will edit
		//UAnimSequence* MirrorSequence = FromAnimSequence;
		const auto& Skel = MirrorSequence->GetSkeleton()->GetReferenceSkeleton();

		int NumMirrorBones = MirrorTable->MirrorBones.Num();

		int NumFrames = MirrorSequence->GetNumberOfFrames();
		TArray<FRawAnimSequenceTrack> SourceRawAnimDatas = MirrorSequence->GetRawAnimationData();
		const auto& TrackNames = MirrorSequence->GetAnimationTrackNames();

		for (int i = 0; i < NumMirrorBones; i++)
		{
			FMirrorBone CurrentBone = MirrorTable->MirrorBones[i];

			if (Skel.FindBoneIndex(CurrentBone.BoneName) == INDEX_NONE)
			{
				continue;
			}

			if (CurrentBone.IsTwinBone)
			{
				if (Skel.FindBoneIndex(CurrentBone.TwinBoneName) == INDEX_NONE)
				{
					continue;
				}

				int32 TrackIndex = TrackNames.IndexOfByKey(CurrentBone.BoneName);
				int32 TwinTrackIndex = TrackNames.IndexOfByKey(CurrentBone.TwinBoneName);

				if (TrackIndex == INDEX_NONE && TwinTrackIndex == INDEX_NONE)
				{
					continue;
				}

				TArray <FVector> MirrorPosKeys;
				TArray <FQuat> MirrorRotKeys;
				TArray <FVector> MirrorScaleKeys;

				TArray <FVector> TwinMirrorPosKeys;
				TArray <FQuat> TwinMirrorRotKeys;
				TArray <FVector> TwinMirrorScaleKeys;

				// Original Bone
				if (TrackIndex != INDEX_NONE)
				{
					auto& MirroredRawTrack = SourceRawAnimDatas[TrackIndex];

					for (int u = 0; u < NumFrames; u++)
					{
						FTransform MirrorTM;

						bool bSetPos = false;
						bool bSetRot = false;
						bool bSetScale = false;

						if (MirroredRawTrack.PosKeys.IsValidIndex(u))
						{
							MirrorTM.SetTranslation(MirroredRawTrack.PosKeys[u]);
							bSetPos = true;
						}
						if (MirroredRawTrack.RotKeys.IsValidIndex(u))
						{
							MirrorTM.SetRotation(MirroredRawTrack.RotKeys[u]);
							bSetRot = true;
						}
						if (MirroredRawTrack.ScaleKeys.IsValidIndex(u))
						{
							MirrorTM.SetScale3D(MirroredRawTrack.ScaleKeys[u]);
							bSetScale = true;
						}

						MirrorTM.Mirror(CurrentBone.MirrorAxis, CurrentBone.FlipAxis);

						FRotator BoneNewRotation = MirrorTM.Rotator();

						BoneNewRotation.Yaw += CurrentBone.RotationOffset.Yaw;
						BoneNewRotation.Roll += CurrentBone.RotationOffset.Roll;
						BoneNewRotation.Pitch += CurrentBone.RotationOffset.Pitch;

						MirrorTM.SetRotation(FQuat(BoneNewRotation));
						MirrorTM.SetScale3D(MirrorTM.GetScale3D().GetAbs());
						MirrorTM.NormalizeRotation();

						if (bSetPos)
						{
							MirrorPosKeys.Add(MirrorTM.GetTranslation());
						}
						if (bSetRot)
						{
							MirrorRotKeys.Add(MirrorTM.GetRotation());
						}
						if (bSetScale)
						{
							MirrorScaleKeys.Add(MirrorTM.GetScale3D());
						}
					}
				}
				else
				{
					auto RefTM = Skel.GetRefBonePose()[Skel.FindBoneIndex(CurrentBone.BoneName)];

					RefTM.Mirror(CurrentBone.MirrorAxis, CurrentBone.FlipAxis);

					FRotator BoneNewRotation = RefTM.Rotator();

					BoneNewRotation.Yaw += CurrentBone.RotationOffset.Yaw;
					BoneNewRotation.Roll += CurrentBone.RotationOffset.Roll;
					BoneNewRotation.Pitch += CurrentBone.RotationOffset.Pitch;

					RefTM.SetRotation(FQuat(BoneNewRotation));
					RefTM.SetScale3D(RefTM.GetScale3D().GetAbs());
					RefTM.NormalizeRotation();

					MirrorPosKeys.Add(RefTM.GetTranslation());
					MirrorRotKeys.Add(RefTM.GetRotation());
				}

				// Twin Bone
				if (TwinTrackIndex != INDEX_NONE)
				{
					auto& TwinMirroredRawTrack = SourceRawAnimDatas[TwinTrackIndex];

					for (int u = 0; u < NumFrames; u++)
					{
						FTransform TwinMirrorTM;

						bool TwinbSetPos = false;
						bool TwinbSetRot = false;
						bool TwinbSetScale = false;

						if (TwinMirroredRawTrack.PosKeys.IsValidIndex(u))
						{
							TwinMirrorTM.SetTranslation(TwinMirroredRawTrack.PosKeys[u]);
							TwinbSetPos = true;
						}
						if (TwinMirroredRawTrack.RotKeys.IsValidIndex(u))
						{
							TwinMirrorTM.SetRotation(TwinMirroredRawTrack.RotKeys[u]);
							TwinbSetRot = true;
						}
						if (TwinMirroredRawTrack.ScaleKeys.IsValidIndex(u))
						{
							TwinMirrorTM.SetScale3D(TwinMirroredRawTrack.ScaleKeys[u]);
							TwinbSetScale = true;
						}

						TwinMirrorTM.Mirror(CurrentBone.MirrorAxis, CurrentBone.FlipAxis);

						FRotator TwinBoneNewRotation = TwinMirrorTM.Rotator();

						TwinBoneNewRotation.Yaw += CurrentBone.RotationOffset.Yaw;
						TwinBoneNewRotation.Roll += CurrentBone.RotationOffset.Roll;
						TwinBoneNewRotation.Pitch += CurrentBone.RotationOffset.Pitch;

						TwinMirrorTM.SetRotation(FQuat(TwinBoneNewRotation));
						TwinMirrorTM.SetScale3D(TwinMirrorTM.GetScale3D().GetAbs());
						TwinMirrorTM.NormalizeRotation();

						if (TwinbSetPos)
						{
							TwinMirrorPosKeys.Add(TwinMirrorTM.GetTranslation());
						}
						if (TwinbSetRot)
						{
							TwinMirrorRotKeys.Add(TwinMirrorTM.GetRotation());
						}
						if (TwinbSetScale)
						{
							TwinMirrorScaleKeys.Add(TwinMirrorTM.GetScale3D());
						}
					}
				}
				else
				{
					auto RefTM = Skel.GetRefBonePose()[Skel.FindBoneIndex(CurrentBone.TwinBoneName)];

					RefTM.Mirror(CurrentBone.MirrorAxis, CurrentBone.FlipAxis);

					FRotator TwinBoneNewRotation = RefTM.Rotator();

					TwinBoneNewRotation.Yaw += CurrentBone.RotationOffset.Yaw;
					TwinBoneNewRotation.Roll += CurrentBone.RotationOffset.Roll;
					TwinBoneNewRotation.Pitch += CurrentBone.RotationOffset.Pitch;

					RefTM.SetRotation(FQuat(TwinBoneNewRotation));
					RefTM.SetScale3D(RefTM.GetScale3D().GetAbs());
					RefTM.NormalizeRotation();

					TwinMirrorPosKeys.Add(RefTM.GetTranslation());
					TwinMirrorRotKeys.Add(RefTM.GetRotation());
				}

				// Original Bone -> Twin Bone
				{
					FRawAnimSequenceTrack NewTrack;

					NewTrack.PosKeys = CurrentBone.MirrorTranslation ? MirrorPosKeys : TwinMirrorPosKeys;
					NewTrack.RotKeys = MirrorRotKeys;
					NewTrack.ScaleKeys = MirrorScaleKeys;

					MirrorSequence->AddNewRawTrack(CurrentBone.TwinBoneName, &NewTrack);
				}

				// Twin Bone -> Original Bone
				{
					FRawAnimSequenceTrack NewTrack;

					NewTrack.PosKeys = CurrentBone.MirrorTranslation ? TwinMirrorPosKeys : MirrorPosKeys;
					NewTrack.RotKeys = TwinMirrorRotKeys;
					NewTrack.ScaleKeys = TwinMirrorScaleKeys;

					MirrorSequence->AddNewRawTrack(CurrentBone.BoneName, &NewTrack);
				}
			}
			else
			{
				int32 TrackIndex = TrackNames.IndexOfByKey(CurrentBone.BoneName);

				if (TrackIndex == INDEX_NONE)
				{
					continue;
				}

				FRawAnimSequenceTrack MirroredRawTrack = SourceRawAnimDatas[TrackIndex];

				//MirrorAllFrames
				TArray <FVector> MirrorPosKeys;
				TArray <FQuat> MirrorRotKeys;
				TArray <FVector> MirrorScaleKeys;

				for (int u = 0; u < NumFrames; u++)
				{
					//Mirror Transform
					FTransform MirrorTM;

					bool bSetPos = false;
					bool bSetRot = false;
					bool bSetScale = false;

					if (MirroredRawTrack.PosKeys.IsValidIndex(u))
					{
						MirrorTM.SetTranslation(MirroredRawTrack.PosKeys[u]);
						bSetPos = true;
					}
					if (MirroredRawTrack.RotKeys.IsValidIndex(u))
					{
						MirrorTM.SetRotation(MirroredRawTrack.RotKeys[u]);
						bSetRot = true;
					}
					if (MirroredRawTrack.ScaleKeys.IsValidIndex(u))
					{
						MirrorTM.SetScale3D(MirroredRawTrack.ScaleKeys[u]);
						bSetScale = true;
					}

					MirrorTM.Mirror(CurrentBone.MirrorAxis, CurrentBone.FlipAxis);

					FRotator BoneNewRotation = MirrorTM.Rotator();

					BoneNewRotation.Yaw += CurrentBone.RotationOffset.Yaw;
					BoneNewRotation.Roll += CurrentBone.RotationOffset.Roll;
					BoneNewRotation.Pitch += CurrentBone.RotationOffset.Pitch;

					MirrorTM.SetRotation(FQuat(BoneNewRotation));
					//MirrorTM.NormalizeRotation();
					MirrorTM.SetScale3D(MirrorTM.GetScale3D().GetAbs());

					MirrorTM.NormalizeRotation();

					//Setting it up Main
					if (bSetPos)
					{
						MirrorPosKeys.Add(MirrorTM.GetTranslation());
					}
					if (bSetRot)
					{
						MirrorRotKeys.Add(MirrorTM.GetRotation());
					}
					if (bSetScale)
					{
						MirrorScaleKeys.Add(MirrorTM.GetScale3D());
					}

					/////////////////////////////////
				}

				MirroredRawTrack.PosKeys = MirrorPosKeys;
				MirroredRawTrack.RotKeys = MirrorRotKeys;
				MirroredRawTrack.ScaleKeys = MirrorScaleKeys;

				//Finally Setting it in the AnimSequence

				MirrorSequence->AddNewRawTrack(CurrentBone.BoneName, &MirroredRawTrack);
			}
		}
		MirrorSequence->ClearBakedTransformData();
		MirrorSequence->RawCurveData.TransformCurves.Empty();
		MirrorSequence->bNeedsRebake = false;
		MirrorSequence->MarkRawDataAsModified();
		MirrorSequence->OnRawDataChanged();
		MirrorSequence->MarkPackageDirty();
	}
}

#endif

#undef LOCTEXT_NAMESPACE