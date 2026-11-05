#pragma once

// Builds one flora graft as a procedural mesh (Godot: flora/vine_graft.gd, bark_graft.gd and
// spore_graft.gd, merged: VoltVine's vines, leaves, thorns and crystal pods; IronBark's bark
// plates and seed pods; VeilSpore's mycelium veins and shelf mushrooms). Each part goes into its
// surface (a mesh section with the matching M_WWO_Graft* material).
//
// Points are world positions (cm) in the standing pose, as in Godot; lengths, widths and
// thicknesses are Godot meters, scaled by Unit (cm per meter: 100 times the body's scale).
// Directions are world vectors. Godot's cross products and rotations are mirrored for Unreal's
// left-handed axes (GCross), so every shape comes out the same way round.

#include "CoreMinimal.h"

class UProceduralMeshComponent;
class UObject;

class FWWOGraftBuilder
{
public:
	enum ESurface { Vine, Leaf, Pod, Bark, Seed, Cap, Gill, NumSurfaces };

	// The leaf sheet rectangles (VineGraft: FROND, FROND_SHORT, BLADE), stem at the bottom edge.
	struct FRect { float X, Y, W, H; };
	static const FRect Frond, FrondShort, Blade;

	FWWOGraftBuilder(const FVector& InOrigin, const FLinearColor& InPodColor, float InUnit, FRandomStream& InRandom);

	FVector Origin;
	FLinearColor PodColor;       // the pods' glow (VeilSpore: the gills' glow)
	FLinearColor VineColor;      // stalks and thorns
	FLinearColor VineGlow = FLinearColor(0, 0, 0, 0); // if A > 0, the stalks glow faintly this color (A = strength)
	float Unit;

	// --- VineGraft ---
	TArray<FVector> Wrap(const FVector& A, const FVector& B, float Radius, float Turns, float Phase = 0.f, float Thickness = 0.009f);
	TArray<FVector> Loop(const FVector& Center, const FVector& AxisA, const FVector& AxisB, float Thickness = 0.008f, float Waviness = 0.f, int32 Points = 28);
	void Tube(const TArray<FVector>& Path, float Thickness, bool bTaper = true);
	void Thorn(const FVector& At, const FVector& Direction, float Length = 0.025f, float Width = 0.006f);
	void AddLeaf(const FVector& At, const FVector& Direction, float Length, const FRect& Rect = Frond, float Fold = 0.35f, float Droop = 0.25f, float Roll = 0.f);
	void LeavesAlong(const TArray<FVector>& Path, const FVector& A, const FVector& B, int32 Count, float Length, float Hang = 0.8f, const FRect& Rect = FrondShort);
	void AddPod(const FVector& At, const FVector& Along, const FVector& Out, float Length, float Width, bool bFrameVine = true);
	// --- BarkGraft ---
	void Plate(const FVector& Center, const FVector& Out, const FVector& Along, float Length, float Width,
		float Curve = 0.3f, float Thickness = 0.022f, float Lift = 0.f, bool bCracked = true);
	static void OnOval(const FVector& A, const FVector& B, const FVector& Side, const FVector& Front, float Rx, float Ry, float T, float Angle,
		float Unit, FVector& OutPoint, FVector& OutNormal);
	void SeedPod(const FVector& Top, float Length, float Width, float Stalk = 0.04f);
	// --- SporeGraft ---
	void Web(const FVector& A, const FVector& B, const FVector& Side, const FVector& Front, float Rx, float Ry,
		FVector2D Start, float Heading, float Length, float Thickness = 0.007f, int32 Depth = 4);
	void Shelf(const FVector& At, const FVector& Out, const FVector& Up, float Width, float Reach, float Thickness = 0.025f);

	bool IsEmpty() const;
	// Fills the mesh with one section per surface used, each with its material.
	void Build(UProceduralMeshComponent* Mesh, UObject* Outer) const;

	// Godot's a.cross(b) for the same physical vectors in Unreal's axes.
	static FVector GCross(const FVector& A, const FVector& B) { return FVector::CrossProduct(B, A); }
	// Two directions square to Direction and to each other (VineGraft._frame).
	static void Frame(const FVector& Direction, FVector& OutSide, FVector& OutUp);

private:
	struct FSurface
	{
		TArray<FVector> Positions;
		TArray<FVector2D> UV0, UV1;
		TArray<FColor> Colors;
		bool bSmooth = true;
	};
	FSurface Surfaces[NumSurfaces];
	FRandomStream& Random;
	FColor Paint = FColor::White;

	// One triangle in Godot's stored order (its front face clockwise in Godot).
	void Tri(ESurface S, const FVector& A, const FVector& B, const FVector& C);
	void TriUV(ESurface S, const FVector* P, const FVector2D* UV, const FVector2D* UV2 = nullptr);
	static FColor Raw(const FLinearColor& C); // a color as raw bytes (Godot reads vertex colors as they are)
};
