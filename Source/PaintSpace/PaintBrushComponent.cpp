// Fill out your copyright notice in the Description page of Project Settings.

#include "PaintSpace.h"
#include "PaintBrushComponent.h"
#include "PaintMaterial.h"


// This class should be attached to some scene component in order to provide paint functionality based on sockets.

UPaintBrushComponent::UPaintBrushComponent()
{
	bWantsBeginPlay = true;
	PrimaryComponentTick.bCanEverTick = true;

	PrevFrameID = -1;
	ProceduralSectionIndex = 0;

	Delay = 0.0f;
	DelayWait = 0.6f;

	PreviousLocation = FVector(0, 0, 0);
	ScaleVector = FVector(0.01f, 0.01f, 0.05f); // experimentally determined, suitable for VR scale

	// this value determines the max dist between instances before interpolating another between them, based on experimentation/estimation and scale
	InterpolationUnitFactor = (10.0f * ScaleVector.Z) / 2.0f;

	StrokeStart = true;
	SprayPainting = false;

	ObjExporter ObjExp = ObjExporter();
	ObjExporterInstance = &ObjExp;
}


void UPaintBrushComponent::BeginPlay()
{
	Super::BeginPlay();


	if (PaintMaterial)
	{
		UWorld* const world = GetWorld();

		if (world)
		{
			// spawn instanced static mesh actor, better performance than individual actors
			PaintMaterialInstance = world->SpawnActor<APaintMaterial>(PaintMaterial);
			LODModel = &PaintMaterialInstance->MeshComponent->StaticMesh->RenderData->LODResources[0];
			VertexBuffer = &LODModel->PositionVertexBuffer;
			//PaintMaterialInstance->ProceduralMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			//PaintMaterialInstance->ProceduralMeshComponent->SetMaterial(0, PaintMaterialInstance->InstanceMesh->Materials[0]->GetMaterial());
		}
	}

	if (SprayPaintFX)
	{
		SprayPaintComponent = UGameplayStatics::SpawnEmitterAttached(SprayPaintFX, this, FName("rt_index_endSocket"), FVector(0, 0, 0), FRotator(0, 0, 0), EAttachLocation::Type::KeepRelativeOffset, false);
		SprayPaintComponent->Deactivate();
		SprayPaintComponent->bAutoActivate = false;
		//SprayPaintComponent->SetTranslucentSortPriority(0);
		SprayPaintComponent->SetHiddenInGame(false);
	}

	if (PenFX)
	{
		PenComponent = UGameplayStatics::SpawnEmitterAttached(PenFX, this, FName("rt_index_endSocket"), FVector(0, 0, 0), FRotator(0, 0, 0), EAttachLocation::Type::KeepRelativeOffset, false);
		PenComponent->Deactivate();
		PenComponent->bAutoActivate = false;
	}
}


void UPaintBrushComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (LeapController.isConnected())
	{
		const Leap::Frame LatestFrame = LeapController.frame();
		if (LatestFrame.id() != PrevFrameID)
		{
			ProcessLeapFrame(LatestFrame, DeltaTime);

			PrevFrameID = LatestFrame.id();
		}

	}

}


void UPaintBrushComponent::ClearAllStrokes()
{
	if (PaintMaterialInstance)
	{
		PaintMaterialInstance->MeshComponent->ClearInstances();
	}
	if (SprayPaintComponent)
	{
		SprayPaintComponent->KillParticlesForced(); // test
	}
}


void UPaintBrushComponent::ExportObjBP()
{
	ExportObj();
}


void UPaintBrushComponent::ProcessLeapFrame(Leap::Frame Frame, float DeltaSeconds)
{
	for (Leap::Hand Hand : Frame.hands())
	{
		if (Hand.isRight())
		{
			Leap::Finger Thumb;
			Leap::Finger Index;

			for (Leap::Finger Finger : Hand.fingers())
			{
				if (Finger.type() == Leap::Finger::Type::TYPE_THUMB)
				{
					Thumb = Finger;
				}
				if (Finger.type() == Leap::Finger::Type::TYPE_INDEX)
				{
					Index = Finger;
				}
			}
			if (Thumb.isValid() && Index.isValid())
			{
				if (!Thumb.isExtended() && Index.isExtended() && Hand.grabStrength() < 0.2f)
				{
					if (Delay < DelayWait)
					{
						Delay += DeltaSeconds;
					}
					else {
						SprayPaint(true);
					}
				}
				else if (Hand.pinchStrength() > 0.8f)
				{
					if (Delay < DelayWait)
					{
						Delay += DeltaSeconds;
					}
					else {
						MeshPaint();
					}
				}
				else
				{
					Delay = 0.0f;
					SprayPaint(false);
					StrokeStart = true;
				}
			}
		}
	}
}


void UPaintBrushComponent::MeshPaint()
{
	//int32 instances = PaintMaterialInstance->MeshComponent->PerInstanceSMData.Num();

	const FVector CurrentLocation = GetComponentLocation();
	FRotator SpawnRotation = FRotator(0, 0, 0);
	FVector Direction = CurrentLocation - PreviousLocation;
	SpawnRotation = FRotationMatrix::MakeFromZ(Direction).Rotator(); // MakeFromZ because we only rotate about Z axis (yaw)

	// Interpolation to improve appearance
	if (!StrokeStart)
	{
		float Dist = Direction.Size();
		int32 InterpolateCount = FMath::CeilToInt(Dist / InterpolationUnitFactor);
		for (int32 i = 1; i <= InterpolateCount; i++)
		{
			const FVector SpawnLocation = ((Direction / (InterpolateCount + 1)) * i) + PreviousLocation;
			PaintMaterialInstance->MeshComponent->AddInstance(FTransform(SpawnRotation, SpawnLocation, ScaleVector));
		}
	}

	// spawn static mesh
	PaintMaterialInstance->MeshComponent->AddInstance(FTransform(SpawnRotation, CurrentLocation, ScaleVector));



	/*if (instances > 1)
	{
		GenerateProceduralMesh(PaintMaterialInstance->MeshComponent->PerInstanceSMData[instances - 2], PaintMaterialInstance->MeshComponent->PerInstanceSMData[instances - 1]);
	}*/
	//FString dbgmsg = FString(PaintMaterialInstance->MeshComponent->PerInstanceSMData.Last().Transform.ToString());

	PreviousLocation = CurrentLocation;

	if (StrokeStart)
	{
		StrokeStart = false;
	}

}


void UPaintBrushComponent::SprayPaint(bool Enable)
{
	if (Enable && !SprayPainting)
	{
		SprayPaintComponent->Activate();
		SprayPainting = true;
	}
	else if (!Enable && SprayPainting)
	{
		SprayPaintComponent->Deactivate();
		SprayPainting = false;
	}
}


// Performance cost too high, not used
void UPaintBrushComponent::GenerateProceduralMesh(FInstancedStaticMeshInstanceData PrevMesh, FInstancedStaticMeshInstanceData CurrentMesh)
{
	FVector ScaleVector = FVector(0.01f, 0.01f, 0.05f);

	FTransform PrevTransform = FTransform(PrevMesh.Transform);
	FTransform CurrentTransform = FTransform(CurrentMesh.Transform);
	bool forward = true;
	if (PrevTransform.GetTranslation().Z > CurrentTransform.GetTranslation().Z)
	{
		forward = false;
	}

	TArray<FVector> ProceduralVertices;
	TArray<int32> ProceduralTriangles;
	TArray<FColor> ProceduralColors;
	TArray<FVector> SecondPartition;

	for (uint32 i = 0; i < VertexBuffer->GetNumVertices() / 2; i += 4)
	{
		if (forward)
		{
			ProceduralVertices.Add(VertexBuffer->VertexPosition(i + 2) * ScaleVector + PrevTransform.GetTranslation());
			ProceduralVertices.Add(VertexBuffer->VertexPosition(i + 3) * ScaleVector + PrevTransform.GetTranslation());
			SecondPartition.Add(VertexBuffer->VertexPosition(i) * ScaleVector + CurrentTransform.GetTranslation());
			SecondPartition.Add(VertexBuffer->VertexPosition(i + 1) * ScaleVector + CurrentTransform.GetTranslation());
		}
		else
		{
			ProceduralVertices.Add(VertexBuffer->VertexPosition(i) * ScaleVector + PrevTransform.GetTranslation());
			ProceduralVertices.Add(VertexBuffer->VertexPosition(i + 1) * ScaleVector + PrevTransform.GetTranslation());
			SecondPartition.Add(VertexBuffer->VertexPosition(i + 2) * ScaleVector + CurrentTransform.GetTranslation());
			SecondPartition.Add(VertexBuffer->VertexPosition(i + 3) * ScaleVector + CurrentTransform.GetTranslation());
		}
	}
	ProceduralVertices.Append(SecondPartition);

	for (int i = 1; i < ProceduralVertices.Num() + 1; i++)
	{
		ProceduralTriangles.Add(i);
		(i % (ProceduralVertices.Num() / 2) == 0) ? ProceduralTriangles.Add(i - (ProceduralVertices.Num() / 2) + 1) : ProceduralTriangles.Add(i + 1);
		(i > ProceduralVertices.Num() / 2) ? ProceduralTriangles.Add(i % (ProceduralVertices.Num() / 2) + 1) : ProceduralTriangles.Add(i + ProceduralVertices.Num() / 2);
	}

	for (int i = 0; i < ProceduralVertices.Num(); i++)
	{
		ProceduralColors.Add(FColor::Red);
	}
	PaintMaterialInstance->ProceduralMeshComponent->CreateMeshSection(ProceduralSectionIndex, ProceduralVertices, ProceduralTriangles, TArray<FVector>(), TArray<FVector2D>(), ProceduralColors, TArray<FProcMeshTangent>(), false);
	ProceduralSectionIndex++;
}


void UPaintBrushComponent::ExportObj()
{
	FString dbgmsgsucceed = FString("Export successful");
	FString dbgmsgfail = FString("-Error exporting-");


	// ensure we are using the right instanced static mesh
	ObjExporterInstance->RegisterStaticMeshComponent(PaintMaterialInstance->MeshComponent);

	if (ObjExporterInstance->ExportObjFile())
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, dbgmsgsucceed);
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red, dbgmsgfail);
	}

}