#include "Characters/WWOShellAnchors.h"
#include "Systems/WWOConfig.h"
#include "WhatWasOurs.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "UObject/ObjectKey.h"

namespace
{
	// Each mesh's reference pose in its own space, by bone index.
	const TArray<FTransform>& RefPose(const USkeletalMesh* Mesh)
	{
		static TMap<FObjectKey, TArray<FTransform>> Cache;
		if (const TArray<FTransform>* Found = Cache.Find(FObjectKey(Mesh))) return *Found;
		TArray<FTransform>& CS = Cache.Add(FObjectKey(Mesh));
		if (!Mesh) return CS;
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const TArray<FTransform>& Local = Ref.GetRefBonePose();
		CS.SetNum(Ref.GetNum());
		for (int32 i = 0; i < Ref.GetNum(); ++i)
		{
			const int32 Parent = Ref.GetParentIndex(i);
			CS[i] = Parent == INDEX_NONE ? Local[i] : Local[i] * CS[Parent];
		}
		return CS;
	}

	FVector Vec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		return WWOConfig::Vec(O, Field, FVector::ZeroVector);
	}

	FName NameOr(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
	{
		FString S;
		return O.IsValid() && O->TryGetStringField(Field, S) && !S.IsEmpty() ? FName(*S) : NAME_None;
	}
}

FTransform WWOShellAnchors::RefBone(const USkeletalMesh* Mesh, FName Bone)
{
	if (!Mesh) return FTransform::Identity;
	const int32 Index = Mesh->GetRefSkeleton().FindBoneIndex(Bone);
	const TArray<FTransform>& CS = RefPose(Mesh);
	return CS.IsValidIndex(Index) ? CS[Index] : FTransform::Identity;
}

FWWOShellPoint WWOShellAnchors::MakePoint(const USkeletalMesh* Mesh, FName Bone, const FVector& P, const FVector& N, const FVector& Up, float Size)
{
	FWWOShellPoint Out;
	Out.Bone = Bone;
	Out.BoneIndex = Mesh ? Mesh->GetRefSkeleton().FindBoneIndex(Bone) : INDEX_NONE;
	Out.RefP = P;
	Out.RefN = N.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
	// 'up' square to the way out (the exported ones nearly are); any square direction if it runs along it.
	FVector U = Up - Out.RefN * FVector::DotProduct(Up, Out.RefN);
	if (!U.Normalize()) U = FVector::CrossProduct(Out.RefN, FMath::Abs(Out.RefN.Z) < 0.9f ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
	Out.RefUp = U;
	Out.Size = Size;
	const FTransform Ref(FRotationMatrix::MakeFromXZ(Out.RefN, Out.RefUp).ToQuat(), P);
	Out.Local = Ref.GetRelativeTransform(RefBone(Mesh, Bone));
	return Out;
}

const FWWOShellAnchorSet* WWOShellAnchors::Find(FName Part, const USkeletalMesh* Mesh)
{
	static TMap<FString, TUniquePtr<FWWOShellAnchorSet>> Cache;
	if (Part.IsNone() || !Mesh) return nullptr;
	const FString Key = Part.ToString() + TEXT("|") + Mesh->GetPathName();
	if (const TUniquePtr<FWWOShellAnchorSet>* Found = Cache.Find(Key)) return Found->Get();
	Verify(Mesh);
	// (Read directly, not through LoadCached: only the converted set is kept.)
	const TSharedPtr<FJsonObject> Json = WWOConfig::Load(WWOConfig::Path(*FString::Printf(TEXT("ShellAnchors/%s.json"), *Part.ToString())));
	if (!Json.IsValid())
	{
		UE_LOG(LogWWO, Warning, TEXT("Shell anchors: no Config/ShellAnchors/%s.json"), *Part.ToString());
		return Cache.Add(Key, nullptr).Get();
	}
	FString Space;
	if (Json->TryGetStringField(TEXT("space"), Space) && Space != TEXT("blender_rig_cm"))
	{
		UE_LOG(LogWWO, Warning, TEXT("Shell anchors: %s is in '%s', not blender_rig_cm; skipped"), *Part.ToString(), *Space);
		return Cache.Add(Key, nullptr).Get();
	}
	TUniquePtr<FWWOShellAnchorSet> Set = MakeUnique<FWWOShellAnchorSet>();
	Set->Part = Part;
	Set->RefMesh = Mesh;
	Json->TryGetStringField(TEXT("slot"), Set->Slot);
	int32 Missing = 0;
	auto Point = [&](const TSharedPtr<FJsonObject>& O, FWWOShellPoint& Out)
	{
		const FName Bone = NameOr(O, TEXT("bone"));
		if (Mesh->GetRefSkeleton().FindBoneIndex(Bone) == INDEX_NONE)
		{
			++Missing;
			return false;
		}
		double Size = 0.0;
		O->TryGetNumberField(TEXT("size"), Size);
		// Paths' points have no 'up': any direction square to the way out (Sample gives them the path's own).
		const FVector Up = O->HasField(TEXT("up")) ? FromRig(Vec3(O, TEXT("up"))) : FVector::UpVector;
		Out = MakePoint(Mesh, Bone, FromRig(Vec3(O, TEXT("p"))), FromRig(Vec3(O, TEXT("n"))), Up, float(Size));
		return true;
	};
	const TSharedPtr<FJsonObject>* Anchors = nullptr;
	if (Json->TryGetObjectField(TEXT("anchors"), Anchors))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*Anchors)->Values)
		{
			FWWOShellPoint P;
			if (Point(It.Value->AsObject(), P)) Set->Anchors.Add(FName(*It.Key), P);
		}
	}
	const TSharedPtr<FJsonObject>* Paths = nullptr;
	if (Json->TryGetObjectField(TEXT("paths"), Paths))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*Paths)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			FWWOShellPath Path;
			O->TryGetBoolField(TEXT("closed"), Path.bClosed);
			double Length = 0.0;
			O->TryGetNumberField(TEXT("length"), Length);
			Path.Length = float(Length);
			Path.FromSeam = NameOr(O, TEXT("from_seam"));
			Path.ToSeam = NameOr(O, TEXT("to_seam"));
			const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
			if (O->TryGetArrayField(TEXT("points"), Points))
			{
				for (const TSharedPtr<FJsonValue>& V : *Points)
				{
					FWWOShellPoint P;
					if (Point(V->AsObject(), P)) Path.Points.Add(P);
				}
			}
			if (Path.Points.Num() >= 2) Set->Paths.Add(FName(*It.Key), MoveTemp(Path));
		}
	}
	// The wrap's samples: [x, y, z, nx, ny, nz, bone index] rows, the bones listed once.
	const TSharedPtr<FJsonObject>* Surface = nullptr;
	if (Json->TryGetObjectField(TEXT("surface"), Surface))
	{
		const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		TArray<FName> BoneNames;
		if ((*Surface)->TryGetArrayField(TEXT("bones"), Bones)) for (const TSharedPtr<FJsonValue>& B : *Bones) BoneNames.Add(FName(*B->AsString()));
		if ((*Surface)->TryGetArrayField(TEXT("points"), Rows))
		{
			Set->Surface.Reserve(Rows->Num());
			for (const TSharedPtr<FJsonValue>& Row : *Rows)
			{
				const TArray<TSharedPtr<FJsonValue>>& V = Row->AsArray();
				if (V.Num() < 7) continue;
				const int32 B = int32(V[6]->AsNumber());
				const FVector N = FromRig(FVector(V[3]->AsNumber(), V[4]->AsNumber(), V[5]->AsNumber())).GetSafeNormal();
				if (!BoneNames.IsValidIndex(B) || N.IsZero()) continue;
				Set->Surface.Add({ FromRig(FVector(V[0]->AsNumber(), V[1]->AsNumber(), V[2]->AsNumber())), N, BoneNames[B] });
			}
		}
	}
	if (Missing > 0) UE_LOG(LogWWO, Warning, TEXT("Shell anchors: %s: %d points name a bone %s lacks; left out"), *Part.ToString(), Missing, *Mesh->GetName());
	UE_LOG(LogWWO, Display, TEXT("Shell anchors: %s: %d anchors, %d paths, %d surface samples"), *Part.ToString(), Set->Anchors.Num(), Set->Paths.Num(), Set->Surface.Num());
	return Cache.Add(Key, MoveTemp(Set)).Get();
}

void WWOShellAnchors::Sample(const FWWOShellPath& Path, const USkeletalMesh* Mesh, float Spacing, float Start, float End, TArray<FWWOShellPoint>& Out)
{
	const TArray<FWWOShellPoint>& Pts = Path.Points;
	const int32 Segments = Path.bClosed ? Pts.Num() : Pts.Num() - 1;
	if (Segments < 1 || Spacing <= 0.f) return;
	TArray<float> At; // arc length at the start of each segment
	float Total = 0.f;
	for (int32 i = 0; i < Segments; ++i)
	{
		At.Add(Total);
		Total += FVector::Dist(Pts[i].RefP, Pts[(i + 1) % Pts.Num()].RefP);
	}
	if (Total <= KINDA_SMALL_NUMBER) return;
	const float From = FMath::Clamp(Start, 0.f, 1.f) * Total, To = FMath::Clamp(End, 0.f, 1.f) * Total;
	int32 Seg = 0;
	for (float S = From; S <= To + KINDA_SMALL_NUMBER; S += Spacing)
	{
		while (Seg + 1 < Segments && At[Seg + 1] <= S) ++Seg;
		const FWWOShellPoint& A = Pts[Seg];
		const FWWOShellPoint& B = Pts[(Seg + 1) % Pts.Num()];
		const float Len = FVector::Dist(A.RefP, B.RefP);
		const float T = Len > KINDA_SMALL_NUMBER ? FMath::Clamp((S - At[Seg]) / Len, 0.f, 1.f) : 0.f;
		const FVector P = FMath::Lerp(A.RefP, B.RefP, T);
		const FVector N = FMath::Lerp(A.RefN, B.RefN, T);
		Out.Add(MakePoint(Mesh, T < 0.5f ? A.Bone : B.Bone, P, N, B.RefP - A.RefP, 0.f));
	}
}

// --- FWWOShellSurface ----------------------------------------------------------------------------

void FWWOShellSurface::Add(const FWWOShellPoint& Point)
{
	Samples.Add({ Point.RefP, Point.RefN, Point.Bone });
}

void FWWOShellSurface::Add(const FSample& Sample)
{
	Samples.Add(Sample);
}

void FWWOShellSurface::Finish()
{
	Grid.Reset();
	for (int32 i = 0; i < Samples.Num(); ++i) Grid.FindOrAdd(Key(Samples[i].P)).Add(i);
}

bool FWWOShellSurface::Project(FVector& P, FVector& N, FName* OutBone) const
{
	// Each near sample's tangent plane says how far P is above the surface there; their weighted mean is
	// taken off along their mean normal, three times over (the samples near the moved point change).
	constexpr float Sigma = 1.6f;
	bool bAny = false;
	for (int32 Pass = 0; Pass < 3; ++Pass)
	{
		for (const float Reach : { 4.f, 10.f })
		{
			const int32 R = FMath::CeilToInt(Reach / Cell);
			const FIntVector K = Key(P);
			double W = 0.0, H = 0.0, Best = 0.0;
			FVector Ns = FVector::ZeroVector;
			for (int32 x = -R; x <= R; ++x) for (int32 y = -R; y <= R; ++y) for (int32 z = -R; z <= R; ++z)
			{
				const TArray<int32>* In = Grid.Find(K + FIntVector(x, y, z));
				if (!In) continue;
				for (const int32 i : *In)
				{
					const FSample& S = Samples[i];
					const double D2 = FVector::DistSquared(S.P, P);
					const double A = FVector::DotProduct(S.N, N);
					if (D2 > Reach * Reach || A < 0.25) continue;
					const double Wi = FMath::Exp(-D2 / (2.0 * Sigma * Sigma)) * FMath::Pow(A, 4.0) + 1e-9;
					W += Wi;
					H += Wi * FVector::DotProduct(P - S.P, S.N);
					Ns += S.N * Wi;
					if (Wi > Best) { Best = Wi; if (OutBone) *OutBone = S.Bone; }
				}
			}
			if (W < 1e-8) continue;
			const FVector Nm = Ns.GetSafeNormal(UE_SMALL_NUMBER, N);
			P -= Nm * (H / W);
			N = Nm;
			bAny = true;
			break;
		}
		if (!bAny) return false;
	}
	return true;
}

void FWWOShellSurface::Walk(FVector& P, FVector& N, const FVector& Dir, float Dist, FName* OutBone) const
{
	const int32 Hops = FMath::Max(1, FMath::CeilToInt(FMath::Abs(Dist) / 1.2f));
	const float Step = Dist / Hops;
	FVector D = Dir;
	for (int32 i = 0; i < Hops; ++i)
	{
		D = (D - N * FVector::DotProduct(D, N)).GetSafeNormal(UE_SMALL_NUMBER, D);
		P += D * Step;
		Project(P, N, OutBone);
	}
}

void WWOShellAnchors::Verify(const USkeletalMesh* Mesh)
{
	static bool bDone = false;
	if (bDone || !Mesh) return;
	bDone = true;
	const TSharedPtr<FJsonObject> Json = WWOConfig::Load(WWOConfig::Path(TEXT("ShellAnchors/SK_RobotSkeleton.json")));
	const TSharedPtr<FJsonObject>* Seams = nullptr;
	if (!Json.IsValid() || !Json->TryGetObjectField(TEXT("anchors"), Seams)) return;
	// Each seam is its bone's head moved by a fixed amount (Art/Shells/tools/anchor_spec.py SEAMS, robot
	// words: outer 6 cm at the shoulders; outer 3 and up 2 at the hips; outer 5 and up 3 at the neck's sides;
	// none at the neck and waist), in rig cm.
	const TMap<FString, FVector> Offsets = {
		{ TEXT("seam_shoulder_l"), FVector(6, 0, 0) }, { TEXT("seam_shoulder_r"), FVector(-6, 0, 0) },
		{ TEXT("seam_hip_l"), FVector(3, 0, 2) }, { TEXT("seam_hip_r"), FVector(-3, 0, 2) },
		{ TEXT("seam_neck_l"), FVector(5, 0, 3) }, { TEXT("seam_neck_r"), FVector(-5, 0, 3) },
	};
	struct FGuess { const TCHAR* Name; TFunction<FVector(const FVector&)> Map; };
	const FGuess Guesses[] = {
		{ TEXT("(x, -y, z) [used]"), [](const FVector& V) { return FromRig(V); } },
		{ TEXT("(x, y, z)"), [](const FVector& V) { return V; } },
		{ TEXT("(-x, y, z)"), [](const FVector& V) { return FVector(-V.X, V.Y, V.Z); } },
		{ TEXT("(-x, -y, z)"), [](const FVector& V) { return FVector(-V.X, -V.Y, V.Z); } },
		{ TEXT("(y, x, z)"), [](const FVector& V) { return FVector(V.Y, V.X, V.Z); } },
		{ TEXT("(-y, x, z)"), [](const FVector& V) { return FVector(-V.Y, V.X, V.Z); } },
	};
	for (const FGuess& G : Guesses)
	{
		float Worst = 0.f, Sum = 0.f;
		int32 Count = 0;
		FString Each;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : (*Seams)->Values)
		{
			const TSharedPtr<FJsonObject> O = It.Value->AsObject();
			const FName Bone = NameOr(O, TEXT("bone"));
			if (Mesh->GetRefSkeleton().FindBoneIndex(Bone) == INDEX_NONE) continue;
			const FVector* Off = Offsets.Find(It.Key);
			const FVector Joint = G.Map(Vec3(O, TEXT("p")) - (Off ? *Off : FVector::ZeroVector));
			const float D = FVector::Dist(Joint, RefBone(Mesh, Bone).GetLocation());
			Worst = FMath::Max(Worst, D);
			Sum += D;
			++Count;
			Each += FString::Printf(TEXT(" %s %.2f"), *It.Key, D);
		}
		UE_LOG(LogWWO, Display, TEXT("Shell anchors: axes %s: seams to their joints in %s, mean %.2f cm, worst %.2f cm:%s"),
			G.Name, *Mesh->GetName(), Count ? Sum / Count : -1.f, Worst, *Each);
	}
	// Where the joints actually are (mesh space), to read the axes off directly.
	for (const TCHAR* Bone : { TEXT("neck_01"), TEXT("spine_01"), TEXT("upperarm_l"), TEXT("thigh_l"), TEXT("foot_l") })
	{
		const FVector L = RefBone(Mesh, Bone).GetLocation();
		UE_LOG(LogWWO, Display, TEXT("Shell anchors: %s ref pose at (%.2f, %.2f, %.2f) in %s"), Bone, L.X, L.Y, L.Z, *Mesh->GetName());
	}
	// The parts' extents against their anchors: the crown's anchor at the head's top, feet at z = 0,
	// the brow and the toes toward +y (the front).
	const FBox Box = Mesh->GetImportedBounds().GetBox();
	UE_LOG(LogWWO, Display, TEXT("Shell anchors: %s bounds (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f)"), *Mesh->GetName(),
		Box.Min.X, Box.Min.Y, Box.Min.Z, Box.Max.X, Box.Max.Y, Box.Max.Z);
	for (const TCHAR* Part : { TEXT("SK_Shell0_Head"), TEXT("SK_Shell0_Legs") })
	{
		const USkeletalMesh* PartMesh = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(TEXT("/Game/WWO/Characters/RobotFrame/Shells/%s.%s"), Part, Part), nullptr, LOAD_NoWarn | LOAD_Quiet);
		const TSharedPtr<FJsonObject> PartJson = WWOConfig::Load(WWOConfig::Path(*FString::Printf(TEXT("ShellAnchors/%s.json"), Part)));
		const TSharedPtr<FJsonObject>* A = nullptr;
		if (!PartMesh || !PartJson.IsValid() || !PartJson->TryGetObjectField(TEXT("anchors"), A)) continue;
		const FBox B = PartMesh->GetImportedBounds().GetBox();
		FString Line;
		for (const TCHAR* Name : { TEXT("head_top"), TEXT("head_front"), TEXT("foot_top_l"), TEXT("ankle_outer_l") })
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (!(*A)->TryGetObjectField(Name, O)) continue;
			const FVector P = FromRig(Vec3(*O, TEXT("p")));
			Line += FString::Printf(TEXT(" %s at (%.1f, %.1f, %.1f)"), Name, P.X, P.Y, P.Z);
		}
		UE_LOG(LogWWO, Display, TEXT("Shell anchors: %s bounds (%.1f, %.1f, %.1f) to (%.1f, %.1f, %.1f);%s"), Part,
			B.Min.X, B.Min.Y, B.Min.Z, B.Max.X, B.Max.Y, B.Max.Z, *Line);
	}
}
