#include "Characters/WWOGraftBuilder.h"
#include "FX/WWOFX.h"
#include "WWOTypes.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FLinearColor LeafTint(0.36f, 0.46f, 0.24f); // darkens the sheet's bright greens to the concept's olive
	constexpr float BarkTextureScale = 3.f;          // bark texture repeats per meter
	const TCHAR* SurfaceMaterials[] = { TEXT("M_WWO_GraftVine"), TEXT("M_WWO_GraftLeaf"), TEXT("M_WWO_GraftPod"), TEXT("M_WWO_GraftBark"),
		TEXT("M_WWO_GraftSeed"), TEXT("M_WWO_GraftCap"), TEXT("M_WWO_GraftGill") };
	const FVector Down(0.f, 0.f, -1.f);
	const FVector Up(0.f, 0.f, 1.f);
}

const FWWOGraftBuilder::FRect FWWOGraftBuilder::Frond = { 0.012f, 0.452f, 0.28f, 0.536f };      // the big serrated fern frond
const FWWOGraftBuilder::FRect FWWOGraftBuilder::FrondShort = { 0.292f, 0.59f, 0.28f, 0.398f };  // a stubbier serrated frond
const FWWOGraftBuilder::FRect FWWOGraftBuilder::Blade = { 0.05f, 0.044f, 0.142f, 0.39f };       // a plain slim leaf

FWWOGraftBuilder::FWWOGraftBuilder(const FVector& InOrigin, const FLinearColor& InPodColor, float InUnit, FRandomStream& InRandom)
	: Origin(InOrigin), PodColor(InPodColor), VineColor(0.09f, 0.11f, 0.045f), Unit(InUnit), Random(InRandom)
{
	Surfaces[Pod].bSmooth = false; // hard facets
	Surfaces[Bark].bSmooth = false;
}

void FWWOGraftBuilder::Frame(const FVector& Direction, FVector& OutSide, FVector& OutUp)
{
	const FVector Dir = Direction.GetSafeNormal();
	// Godot: Vector3.UP, or Vector3.RIGHT (Unreal +Y) for a near-vertical direction.
	OutSide = GCross(Dir, FMath::Abs(Dir.Z) < 0.95f ? Up : FVector(0.f, 1.f, 0.f)).GetSafeNormal();
	OutUp = GCross(OutSide, Dir).GetSafeNormal();
}

FColor FWWOGraftBuilder::Raw(const FLinearColor& C)
{
	return C.QuantizeRound();
}

void FWWOGraftBuilder::Tri(ESurface S, const FVector& A, const FVector& B, const FVector& C)
{
	// VineGraft._tri: Godot stores a, c, b (clockwise front faces).
	const FVector P[3] = { A, C, B };
	TriUV(S, P, nullptr);
}

void FWWOGraftBuilder::TriUV(ESurface S, const FVector* P, const FVector2D* UV, const FVector2D* UV2)
{
	FSurface& Surf = Surfaces[S];
	for (int32 k = 0; k < 3; ++k)
	{
		Surf.Positions.Add(P[k]);
		Surf.UV0.Add(UV ? UV[k] : FVector2D::ZeroVector);
		Surf.UV1.Add(UV2 ? UV2[k] : FVector2D::ZeroVector);
		Surf.Colors.Add(Paint);
	}
}

// --- Vines -------------------------------------------------------------------------------------

TArray<FVector> FWWOGraftBuilder::Wrap(const FVector& A, const FVector& B, float Radius, float Turns, float Phase, float Thickness)
{
	// A vine spiraling around the line from A to B (a limb), Radius out from its middle.
	const FVector Axis = B - A;
	FVector Side, FUp;
	Frame(Axis, Side, FUp);
	const int32 Steps = FMath::Max(int32(Turns * 16.f), 4) + 2;
	TArray<FVector> Path;
	for (int32 i = 0; i <= Steps; ++i)
	{
		const float T = float(i) / Steps;
		const float Angle = Phase + UE_TWO_PI * Turns * T;
		const float Wobble = 1.f + 0.12f * FMath::Sin(T * 23.f + Phase * 5.f); // not a perfect spring
		Path.Add(A + Axis * T + (Side * FMath::Cos(Angle) + FUp * FMath::Sin(Angle)) * Radius * Unit * Wobble);
	}
	Tube(Path, Thickness);
	return Path;
}

TArray<FVector> FWWOGraftBuilder::Loop(const FVector& Center, const FVector& AxisA, const FVector& AxisB, float Thickness, float Waviness, int32 Points)
{
	// A closed vine loop: an ellipse with half-axes AxisA and AxisB (cm) around Center.
	TArray<FVector> Path;
	const FVector Normal = GCross(AxisA, AxisB).GetSafeNormal();
	for (int32 i = 0; i <= Points; ++i)
	{
		const float Angle = UE_TWO_PI * i / Points;
		const FVector Wave = Normal * FMath::Sin(Angle * 5.f) * Waviness * Unit;
		Path.Add(Center + AxisA * FMath::Cos(Angle) + AxisB * FMath::Sin(Angle) + Wave);
	}
	Tube(Path, Thickness, false);
	return Path;
}

void FWWOGraftBuilder::Tube(const TArray<FVector>& Path, float Thickness, bool bTaper)
{
	// A round stalk along a path (5 sides), thinning out at the ends when bTaper.
	constexpr int32 Sides = 5;
	if (Path.Num() < 2) return;
	FVector Normal = FVector::ZeroVector;
	TArray<TArray<FVector>> Rings;
	const int32 Last = Path.Num() - 1;
	for (int32 i = 0; i <= Last; ++i)
	{
		const FVector Tangent = (Path[FMath::Min(i + 1, Last)] - Path[FMath::Max(i - 1, 0)]).GetSafeNormal();
		if (Normal.IsZero())
		{
			FVector Unused;
			Frame(Tangent, Normal, Unused);
		}
		Normal = (Normal - Tangent * FVector::DotProduct(Normal, Tangent)).GetSafeNormal(); // carried along without twisting
		const FVector Binormal = GCross(Tangent, Normal);
		const float T = float(i) / Last;
		const float R = Thickness * Unit * (bTaper ? FMath::Clamp(FMath::Min(T, 1.f - T) * 6.f, 0.25f, 1.f) : 1.f);
		TArray<FVector>& Ring = Rings.AddDefaulted_GetRef();
		for (int32 s = 0; s < Sides; ++s)
		{
			const float Angle = UE_TWO_PI * s / Sides;
			Ring.Add(Path[i] + (Normal * FMath::Cos(Angle) + Binormal * FMath::Sin(Angle)) * R);
		}
	}
	Paint = Raw(VineColor);
	for (int32 i = 0; i < Rings.Num() - 1; ++i)
	{
		for (int32 s = 0; s < Sides; ++s)
		{
			const int32 n = (s + 1) % Sides;
			Tri(Vine, Rings[i][s], Rings[i + 1][s], Rings[i + 1][n]);
			Tri(Vine, Rings[i][s], Rings[i + 1][n], Rings[i][n]);
		}
	}
}

void FWWOGraftBuilder::Thorn(const FVector& At, const FVector& Direction, float Length, float Width)
{
	// A small hooked thorn (a four-sided spike).
	const FVector Dir = Direction.GetSafeNormal();
	FVector F0, F1;
	Frame(Dir, F0, F1);
	const FVector Tip = At + Dir * Length * Unit + F0 * Length * Unit * 0.25f;
	FVector Base[4];
	for (int32 s = 0; s < 4; ++s)
	{
		const float Angle = UE_TWO_PI * s / 4.f;
		Base[s] = At + (F0 * FMath::Cos(Angle) + F1 * FMath::Sin(Angle)) * Width * Unit;
	}
	Paint = Raw(WWO::Darkened(VineColor, 0.2f));
	for (int32 s = 0; s < 4; ++s) Tri(Vine, Base[s], Tip, Base[(s + 1) % 4]);
}

// --- Leaves ------------------------------------------------------------------------------------

void FWWOGraftBuilder::AddLeaf(const FVector& At, const FVector& Direction, float Length, const FRect& Rect, float Fold, float Droop, float Roll)
{
	// A leaf card: stem at At, pointing along Direction, creased down its middle rib (Fold) and
	// sagging toward its tip (Droop). Roll turns it around its own rib.
	const FVector Dir = Direction.GetSafeNormal();
	FVector F0, F1;
	Frame(Dir, F0, F1);
	const FVector Side = F0.RotateAngleAxisRad(-Roll, Dir); // Godot's rotation, mirrored
	const FVector Face = GCross(Dir, Side).GetSafeNormal();
	const float L = Length * Unit;
	const float W = L * Rect.W / Rect.H;
	FVector Grid[9];
	FVector2D UVs[9];
	for (int32 Row = 0; Row < 3; ++Row)
	{
		const float V = Row / 2.f;
		for (int32 Col = 0; Col < 3; ++Col)
		{
			const float C = Col - 1.f;
			Grid[Row * 3 + Col] = At + Dir * L * V + Side * W * 0.5f * C + Face * Fold * W * 0.5f * FMath::Abs(C) + Down * Droop * L * V * V;
			UVs[Row * 3 + Col] = FVector2D(Rect.X + (C * 0.5f + 0.5f) * Rect.W, Rect.Y + Rect.H - V * Rect.H);
		}
	}
	Paint = Raw(FMath::Lerp(LeafTint, WWO::Lightened(LeafTint, 0.2f), Random.FRand()));
	for (int32 Row = 0; Row < 2; ++Row)
	{
		for (int32 Col = 0; Col < 2; ++Col)
		{
			const int32 i = Row * 3 + Col;
			// VineGraft._quad: points [i, i+3, i+4, i+1], stored as 0 2 1, 0 3 2.
			const int32 Q[4] = { i, i + 3, i + 4, i + 1 };
			const FVector P1[3] = { Grid[Q[0]], Grid[Q[2]], Grid[Q[1]] };
			const FVector2D U1[3] = { UVs[Q[0]], UVs[Q[2]], UVs[Q[1]] };
			TriUV(Leaf, P1, U1);
			const FVector P2[3] = { Grid[Q[0]], Grid[Q[3]], Grid[Q[2]] };
			const FVector2D U2[3] = { UVs[Q[0]], UVs[Q[3]], UVs[Q[2]] };
			TriUV(Leaf, P2, U2);
		}
	}
}

void FWWOGraftBuilder::LeavesAlong(const TArray<FVector>& Path, const FVector& A, const FVector& B, int32 Count, float Length, float Hang, const FRect& Rect)
{
	// Leaves sprouting off a wrapped vine, spread along it, pointing out from the limb (the line
	// A-B) and down it toward B.
	const FVector Axis = (B - A).GetSafeNormal();
	for (int32 i = 0; i < Count; ++i)
	{
		const int32 Index = int32((i + 0.5f) / Count * (Path.Num() - 1));
		const FVector At = Path[Index];
		const FVector Center = A + Axis * FVector::DotProduct(At - A, Axis);
		const FVector Out = (At - Center).GetSafeNormal();
		const FVector Jitter(Random.FRandRange(-0.2f, 0.2f), Random.FRandRange(-0.2f, 0.2f), Random.FRandRange(-0.2f, 0.2f));
		const FVector Dir = (Out + Axis * Hang + Jitter).GetSafeNormal();
		const float Len = Length * Random.FRandRange(0.8f, 1.15f);
		AddLeaf(At, Dir, Len, Rect, 0.35f, 0.2f, Random.FRandRange(-0.5f, 0.5f));
	}
}

// --- Crystal pods ------------------------------------------------------------------------------

void FWWOGraftBuilder::AddPod(const FVector& At, const FVector& Along, const FVector& Out, float Length, float Width, bool bFrameVine)
{
	// An eye-shaped faceted crystal lying flat: long along Along, its face toward Out. bFrameVine
	// wraps a thin loop of vine around its rim, like the concept's leaf-eyes.
	const FVector Axis = Along.GetSafeNormal();
	const FVector Face = (Out - Axis * FVector::DotProduct(Out, Axis)).GetSafeNormal();
	const FVector Across = GCross(Face, Axis).GetSafeNormal();
	Paint = Raw(FLinearColor(Random.FRand(), 0.f, 0.f)); // each pod pulses on its own beat
	constexpr int32 Sides = 6;
	const float L = Length * Unit, W = Width * Unit;
	const float Profile[5][2] = { { -0.5f, 0.f }, { -0.27f, 0.72f }, { 0.f, 1.f }, { 0.27f, 0.72f }, { 0.5f, 0.f } };
	FVector Rings[5][Sides];
	for (int32 r = 0; r < 5; ++r)
	{
		for (int32 s = 0; s < Sides; ++s)
		{
			const float Angle = UE_TWO_PI * (s + 0.5f) / Sides;
			Rings[r][s] = At + Axis * L * Profile[r][0] + (Across * FMath::Cos(Angle) + Face * FMath::Sin(Angle) * 0.55f) * W * Profile[r][1];
		}
	}
	for (int32 i = 0; i < 4; ++i)
	{
		for (int32 s = 0; s < Sides; ++s)
		{
			const int32 n = (s + 1) % Sides;
			if (i == 0) Tri(Pod, Rings[0][0], Rings[1][n], Rings[1][s]);
			else if (i == 3) Tri(Pod, Rings[i][s], Rings[i][n], Rings[i + 1][0]);
			else
			{
				Tri(Pod, Rings[i][s], Rings[i][n], Rings[i + 1][n]);
				Tri(Pod, Rings[i][s], Rings[i + 1][n], Rings[i + 1][s]);
			}
		}
	}
	if (bFrameVine)
	{
		TArray<FVector> Rim;
		for (int32 i = 0; i < 25; ++i)
		{
			const float Angle = UE_TWO_PI * i / 24.f;
			const float Y = FMath::Sin(Angle) * 0.5f * L * 1.08f;
			const float X = FMath::Cos(Angle) * W * 1.12f * (1.f - FMath::Pow(FMath::Abs(FMath::Sin(Angle)), 3.f) * 0.85f);
			Rim.Add(At + Axis * Y + Across * X - Face * W * 0.1f);
		}
		Tube(Rim, Width * 0.14f, false);
	}
}

// --- Bark plates -------------------------------------------------------------------------------

void FWWOGraftBuilder::Plate(const FVector& Center, const FVector& Out, const FVector& Along, float Length, float Width,
	float Curve, float Thickness, float Lift, bool bCracked)
{
	// A bark slab centered at Center, facing Out, running Length along Along and Width across.
	// It bends around its length with radius Curve (small to wrap a limb, large for a nearly flat
	// scale), narrows to blunt points at both ends, and its far end (toward +Along) is lifted off
	// the body by Lift, so rows of plates overlap like scales.
	const FVector D = Along.GetSafeNormal();
	const FVector N = (Out - D * FVector::DotProduct(Out, D)).GetSafeNormal();
	const FVector Across = GCross(N, D).GetSafeNormal();
	constexpr int32 Rows = 6, Cols = 5;
	const float SeedValue = Random.FRand();
	TArray<FVector> Outer, Inner;
	TArray<FVector2D> UV, UV2;
	for (int32 Row = 0; Row <= Rows; ++Row)
	{
		const float V = float(Row) / Rows - 0.5f;
		const float Taper = 1.f - FMath::Pow(FMath::Abs(V * 2.f), 2.4f) * (V > 0.f ? 0.78f : 0.6f);
		const float Half = Width * 0.5f * Taper;
		for (int32 Col = 0; Col <= Cols; ++Col)
		{
			const float U = float(Col) / Cols * 2.f - 1.f;
			const float Angle = U * Half / Curve;
			const FVector Normal = Across * FMath::Sin(Angle) + N * FMath::Cos(Angle);
			const FVector Base = Center + D * V * Length * Unit + Across * FMath::Sin(Angle) * Curve * Unit
				+ N * (FMath::Cos(Angle) - 1.f) * Curve * Unit + N * Lift * Unit * (V + 0.5f);
			const float Bump = (Col > 0 && Col < Cols && Row > 0 && Row < Rows) ? Thickness * Random.FRandRange(-0.35f, 0.35f) : 0.f;
			Inner.Add(Base);
			Outer.Add(Base + Normal * (Thickness + Bump) * Unit * (0.5f + 0.5f * Taper));
			UV.Add(FVector2D(U * Half, V * Length) * BarkTextureScale + FVector2D(SeedValue * 7.f, SeedValue * 3.f));
			UV2.Add(FVector2D(U * 0.5f + 0.5f, V + 0.5f));
		}
	}
	const FColor FaceColor = Raw(FLinearColor(SeedValue, 0.f, bCracked ? 1.f : 0.f));
	const FColor Wall = Raw(FLinearColor(SeedValue, 1.f, 0.f));
	auto Quad = [&](const TArray<FVector>& Points, const int32 (&Q)[4], bool bFlip)
	{
		// BarkGraft._bark_quad: stored 0 2 1, 0 3 2 (or 0 1 2, 0 2 3 for the underside).
		const int32 Order[2][6] = { { 0, 2, 1, 0, 3, 2 }, { 0, 1, 2, 0, 2, 3 } };
		for (int32 t = 0; t < 2; ++t)
		{
			FVector P[3];
			FVector2D U0[3], U1[3];
			for (int32 k = 0; k < 3; ++k)
			{
				const int32 Idx = Q[Order[bFlip][t * 3 + k]];
				P[k] = Points[Idx];
				U0[k] = UV[Idx];
				U1[k] = UV2[Idx];
			}
			TriUV(Bark, P, U0, U1);
		}
	};
	Paint = FaceColor;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Cols; ++Col)
		{
			const int32 i = Row * (Cols + 1) + Col;
			const int32 Q[4] = { i, i + Cols + 1, i + Cols + 2, i + 1 };
			Quad(Outer, Q, false);
			Quad(Inner, Q, true);
		}
	}
	// The edges all the way round, so the slab has visible thickness.
	TArray<int32> Ring;
	for (int32 Col = 0; Col <= Cols; ++Col) Ring.Add(Col);
	for (int32 Row = 1; Row <= Rows; ++Row) Ring.Add(Row * (Cols + 1) + Cols);
	for (int32 Col = Cols - 1; Col >= 0; --Col) Ring.Add(Rows * (Cols + 1) + Col);
	for (int32 Row = Rows - 1; Row > 0; --Row) Ring.Add(Row * (Cols + 1));
	Paint = Wall;
	for (int32 k = 0; k < Ring.Num(); ++k)
	{
		const int32 a = Ring[k], b = Ring[(k + 1) % Ring.Num()];
		const FVector Pts[4] = { Inner[a], Inner[b], Outer[b], Outer[a] };
		const FVector2D Uvs[4] = { UV[a], UV[b], UV[b] + FVector2D(0.f, 0.05f), UV[a] + FVector2D(0.f, 0.05f) };
		const int32 Order[6] = { 0, 2, 1, 0, 3, 2 };
		for (int32 t = 0; t < 2; ++t)
		{
			FVector P[3];
			FVector2D U0[3], U1[3];
			for (int32 j = 0; j < 3; ++j)
			{
				P[j] = Pts[Order[t * 3 + j]];
				U0[j] = Uvs[Order[t * 3 + j]];
				U1[j] = UV2[a];
			}
			TriUV(Bark, P, U0, U1);
		}
	}
}

void FWWOGraftBuilder::OnOval(const FVector& A, const FVector& B, const FVector& Side, const FVector& Front, float Rx, float Ry, float T, float Angle,
	float InUnit, FVector& OutPoint, FVector& OutNormal)
{
	// A point on an oval tube around A-B (the torso), and the way out from its surface.
	OutPoint = A + (B - A) * T + Side * FMath::Cos(Angle) * Rx * InUnit + Front * FMath::Sin(Angle) * Ry * InUnit;
	OutNormal = (Side * FMath::Cos(Angle) / Rx + Front * FMath::Sin(Angle) / Ry).GetSafeNormal();
}

void FWWOGraftBuilder::SeedPod(const FVector& Top, float Length, float Width, float Stalk, const FVector& Hang)
{
	// A glossy orange teardrop hanging below Top on a short stalk (along Hang when given: a pod leaning on the body).
	const FVector H = Hang.IsNearlyZero() ? Down : Hang.GetSafeNormal();
	const FQuat Lean = FQuat::FindBetweenNormals(Down, H);
	const FVector Start = Top + H * Stalk * Unit;
	// Godot's little (0.004, 0, 0.003) kink, in Unreal's axes.
	Tube({ Top, FMath::Lerp(Top, Start, 0.5f) + Lean.RotateVector(FVector(-0.003f, 0.004f, 0.f)) * Unit, Start - H * 0.005f * Unit }, 0.005f, false);
	constexpr int32 Sides = 9, NumRings = 9;
	TArray<TArray<FVector>> Rings;
	for (int32 r = 0; r <= NumRings; ++r)
	{
		const float T = float(r) / NumRings;
		const float Radius = Width * Unit * FMath::Pow(FMath::Max(FMath::Sin(UE_PI * FMath::Pow(T, 1.35f)), 0.f), 0.7f);
		TArray<FVector>& Ring = Rings.AddDefaulted_GetRef();
		for (int32 s = 0; s < Sides; ++s)
		{
			const float Angle = UE_TWO_PI * s / Sides;
			Ring.Add(Start + H * T * Length * Unit + Lean.RotateVector(FVector(-FMath::Sin(Angle), FMath::Cos(Angle), 0.f)) * Radius); // Godot (cos, 0, sin)
		}
	}
	Paint = FColor::White;
	for (int32 r = 0; r < NumRings; ++r)
	{
		for (int32 s = 0; s < Sides; ++s)
		{
			const int32 m = (s + 1) % Sides;
			const FVector P1[3] = { Rings[r][s], Rings[r][m], Rings[r + 1][m] };
			const FVector P2[3] = { Rings[r][s], Rings[r + 1][m], Rings[r + 1][s] };
			TriUV(Seed, P1, nullptr);
			TriUV(Seed, P2, nullptr);
		}
	}
}

// --- Mycelium and shelf mushrooms --------------------------------------------------------------

void FWWOGraftBuilder::Web(const FVector& A, const FVector& B, const FVector& Side, const FVector& Front, float Rx, float Ry,
	FVector2D Start, float Heading, float Length, float Thickness, int32 Depth)
{
	// A branching vein crawling over a limb or the torso, treated as an oval tube around the line
	// A-B (half-widths Rx along Side, Ry along Front). It starts at Start = (how far along A-B, 0-1;
	// angle around in radians) and sets off at Heading (0 = toward B, PI/2 = around), wandering and
	// splitting into thinner branches.
	const FVector Axis = B - A;
	const float Span = Axis.Size() / Unit; // meters
	if (Span < KINDA_SMALL_NUMBER) return;
	constexpr float Step = 0.014f;
	float T = Start.X, Angle = Start.Y, Travelled = 0.f;
	TArray<FVector> Path;
	float NextSplit = Random.FRandRange(0.025f, 0.05f);
	while (Travelled < Length)
	{
		const float Lift = Thickness * 0.6f;
		Path.Add(A + Axis * T + Side * FMath::Cos(Angle) * (Rx + Lift) * Unit + Front * FMath::Sin(Angle) * (Ry + Lift) * Unit);
		Heading += Random.FRandRange(-0.7f, 0.7f);
		T += Step * FMath::Cos(Heading) / Span;
		Angle += Step * FMath::Sin(Heading) / ((Rx + Ry) * 0.5f);
		Travelled += Step;
		if (T < 0.f || T > 1.f) break;
		if (Depth > 0 && Travelled > NextSplit)
		{
			NextSplit = Travelled + Random.FRandRange(0.03f, 0.06f);
			const float Turn = Random.FRandRange(0.6f, 1.2f) * (Random.FRand() < 0.5f ? 1.f : -1.f);
			const float Rest = (Length - Travelled) * Random.FRandRange(0.4f, 0.7f);
			Web(A, B, Side, Front, Rx, Ry, FVector2D(T, Angle), Heading + Turn, Rest, Thickness * 0.72f, Depth - 1);
		}
	}
	Tube(Path, Thickness);
}

void FWWOGraftBuilder::Shelf(const FVector& At, const FVector& Out, const FVector& InUp, float Width, float Reach, float Thickness)
{
	// A fan-shaped bracket growing out of the body at At: it sticks out along Out (Reach meters),
	// spans Width across, domes up along InUp, and its rim sags and ripples.
	const FVector O = Out.GetSafeNormal();
	const FVector Across = GCross(InUp, O).GetSafeNormal();
	const FVector U = GCross(O, Across).GetSafeNormal();
	constexpr int32 Rows = 6, Cols = 14;
	const float WavePhase = Random.FRand() * UE_TWO_PI;
	TArray<FVector> Top, Bottom;
	TArray<FVector2D> UVs;
	for (int32 Row = 0; Row <= Rows; ++Row)
	{
		const float R = float(Row) / Rows;
		for (int32 Col = 0; Col <= Cols; ++Col)
		{
			const float C = float(Col) / Cols;
			const float Phi = (C - 0.5f) * UE_PI * 0.95f;
			const FVector Spread = O * FMath::Cos(Phi) * Reach * Unit + Across * FMath::Sin(Phi) * Width * 0.5f * Unit;
			const float Ripple = FMath::Sin(Phi * 7.f + WavePhase) * 0.12f * Thickness * FMath::Pow(R, 3.f) * 4.f;
			const FVector Sag = -U * FMath::Pow(R, 2.5f) * Thickness * 1.2f * Unit;
			const FVector Base = At + Spread * (0.08f + 0.92f * R) + Sag + U * Ripple * Unit;
			Top.Add(Base + U * Thickness * Unit * (1.f - R * R));
			Bottom.Add(Base - U * Thickness * Unit * 0.25f * (1.f - R * R));
			UVs.Add(FVector2D(C, R));
		}
	}
	const int32 Order[2][6] = { { 0, 2, 1, 0, 3, 2 }, { 0, 1, 2, 0, 2, 3 } };
	Paint = FColor::White;
	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Col = 0; Col < Cols; ++Col)
		{
			const int32 i = Row * (Cols + 1) + Col;
			const int32 Q[4] = { i, i + Cols + 1, i + Cols + 2, i + 1 };
			for (int32 Pass = 0; Pass < 2; ++Pass) // the cap on top, the gills underneath (flipped)
			{
				const TArray<FVector>& Points = Pass == 0 ? Top : Bottom;
				for (int32 t = 0; t < 2; ++t)
				{
					FVector P[3];
					FVector2D U0[3];
					for (int32 k = 0; k < 3; ++k)
					{
						const int32 Idx = Q[Order[Pass][t * 3 + k]];
						P[k] = Points[Idx];
						U0[k] = UVs[Idx];
					}
					TriUV(Pass == 0 ? Cap : Gill, P, U0);
				}
			}
		}
	}
}

// --- Building ----------------------------------------------------------------------------------

bool FWWOGraftBuilder::IsEmpty() const
{
	for (const FSurface& S : Surfaces) if (S.Positions.Num() > 0) return false;
	return true;
}

void FWWOGraftBuilder::Build(UProceduralMeshComponent* Mesh, UObject* Outer) const
{
	int32 Section = 0;
	for (int32 s = 0; s < NumSurfaces; ++s)
	{
		const FSurface& Surf = Surfaces[s];
		const int32 Num = Surf.Positions.Num();
		if (Num == 0) continue;
		TArray<FVector> Vertices, Normals;
		TArray<int32> Triangles;
		TArray<FProcMeshTangent> Tangents;
		Vertices.SetNumUninitialized(Num);
		Normals.SetNumZeroed(Num);
		Triangles.SetNumUninitialized(Num);
		Tangents.SetNum(Num);
		TMap<FIntVector, FVector> Smooth; // summed face normals by (rounded) position
		auto Key = [](const FVector& P) { return FIntVector(FMath::RoundToInt(P.X * 100.f), FMath::RoundToInt(P.Y * 100.f), FMath::RoundToInt(P.Z * 100.f)); };
		for (int32 i = 0; i < Num; i += 3)
		{
			const FVector& A = Surf.Positions[i];
			const FVector& B = Surf.Positions[i + 1];
			const FVector& C = Surf.Positions[i + 2];
			// Godot stores front faces clockwise; mirrored into Unreal's axes the same face's normal
			// is (B - A) x (C - A), and Unreal draws the face whose normal is (P2 - P0) x (P1 - P0),
			// so the indices go A, C, B.
			const FVector Face = FVector::CrossProduct(B - A, C - A);
			for (int32 k = 0; k < 3; ++k)
			{
				Vertices[i + k] = Surf.Positions[i + k] - Origin;
				Normals[i + k] = Face.GetSafeNormal();
				if (Surf.bSmooth) Smooth.FindOrAdd(Key(Surf.Positions[i + k])) += Face;
			}
			Triangles[i] = i;
			Triangles[i + 1] = i + 2;
			Triangles[i + 2] = i + 1;
			// The tangent (along U) for the bark's normal map.
			const FVector2D& UA = Surf.UV0[i];
			const FVector2D DU1 = Surf.UV0[i + 1] - UA, DU2 = Surf.UV0[i + 2] - UA;
			const float Det = DU1.X * DU2.Y - DU2.X * DU1.Y;
			if (FMath::Abs(Det) > SMALL_NUMBER)
			{
				const FVector E1 = B - A, E2 = C - A;
				const FVector TX = ((E1 * DU2.Y - E2 * DU1.Y) / Det);
				const FVector TY = ((E2 * DU1.X - E1 * DU2.X) / Det);
				const FVector N = Face.GetSafeNormal();
				const FVector TanX = (TX - N * FVector::DotProduct(TX, N)).GetSafeNormal();
				const bool bFlip = FVector::DotProduct(FVector::CrossProduct(N, TanX), TY) < 0.f;
				for (int32 k = 0; k < 3; ++k) Tangents[i + k] = FProcMeshTangent(TanX, bFlip);
			}
		}
		if (Surf.bSmooth)
		{
			for (int32 i = 0; i < Num; ++i)
			{
				if (const FVector* Sum = Smooth.Find(Key(Surf.Positions[i])))
				{
					const FVector N = Sum->GetSafeNormal();
					if (!N.IsZero()) Normals[i] = N;
				}
			}
		}
		Mesh->CreateMeshSection(Section, Vertices, Triangles, Normals, Surf.UV0, Surf.UV1, TArray<FVector2D>(), TArray<FVector2D>(), Surf.Colors, Tangents, false);
		UMaterialInstanceDynamic* Mat = UMaterialInstanceDynamic::Create(WWOFX::Material(SurfaceMaterials[s]), Outer);
		if (s == Pod || s == Gill) Mat->SetVectorParameterValue("Color", PodColor);
		if (s == Vine && VineGlow.A > 0.f)
		{
			Mat->SetVectorParameterValue("Glow Color", FLinearColor(VineGlow.R, VineGlow.G, VineGlow.B, 1.f));
			Mat->SetScalarParameterValue("Glow Strength", VineGlow.A);
		}
		Mesh->SetMaterial(Section, Mat);
		++Section;
	}
}
