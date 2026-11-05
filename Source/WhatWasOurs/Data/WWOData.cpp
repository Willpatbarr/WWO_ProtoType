#include "Data/WWOData.h"
#include "Systems/WWOLoadoutSubsystem.h"

float UWWOFramePartData::Points(FName Group) const
{
	if (Group == "agility") return Agility;
	if (Group == "aerial") return Aerial;
	if (Group == "dexterity") return Dexterity;
	if (Group == "durability") return Durability;
	return 0.f;
}

FName UWWOFramePartData::SpikeGroup() const
{
	FName Best = NAME_None;
	float BestPoints = 0.f;
	for (const FName& Group : UWWOLoadoutSubsystem::GroupOrder())
	{
		if (Points(Group) > BestPoints)
		{
			Best = Group;
			BestPoints = Points(Group);
		}
	}
	return Best;
}

FLinearColor UWWOFramePartData::GroupColor(FName Group)
{
	if (Group == "agility") return FLinearColor(0.3f, 0.75f, 0.35f);   // green
	if (Group == "aerial") return FLinearColor(0.35f, 0.75f, 0.95f);    // sky blue
	if (Group == "dexterity") return FLinearColor(0.95f, 0.75f, 0.2f);  // gold
	if (Group == "durability") return FLinearColor(0.62f, 0.62f, 0.66f); // steel gray
	return FLinearColor::White;
}

FLinearColor UWWOFramePartData::BodyColor(const FLinearColor& Base) const
{
	const FName Group = SpikeGroup();
	return Group.IsNone() ? Base : GroupColor(Group);
}

FString UWWOFramePartData::Summary() const
{
	TArray<FString> Lines;
	for (const FName& Group : UWWOLoadoutSubsystem::GroupOrder())
	{
		const float Pts = Points(Group);
		if (FMath::Abs(Pts) >= 0.01f)
		{
			Lines.Add(FString::Printf(TEXT("%s %+d"), *UWWOLoadoutSubsystem::GroupTitle(Group), FMath::RoundToInt(Pts)));
		}
	}
	return Lines.Num() == 0 ? TEXT("No changes") : FString::Join(Lines, TEXT("\n"));
}
