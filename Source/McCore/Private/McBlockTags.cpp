#include "McBlockTags.h"
#include "HAL/FileManager.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McDataPacks.h"
#include "McCore.h"
#include "Misc/Paths.h"

FMcBlockTags& FMcBlockTags::Get()
{
	static FMcBlockTags Instance;
	return Instance;
}

bool FMcBlockTags::EnsureLoaded()
{
	check(IsInGameThread());
	if (bLoaded)
	{
		return true;
	}
	if (!FMcBlockRegistry::Get().EnsureLoaded())
	{
		return false;
	}
	// Tags can sit in subfolders (e.g. mineable/pickaxe); their names keep the folder. A data pack's files add to the game's.
	Pending.Empty();
	Resolved.Empty();
	const TArray<McData::FFile> Files = McData::List(TEXT("tags/block"));
	if (Files.IsEmpty())
	{
		UE_LOG(LogMcCore, Error, TEXT("No block tags in %s; run tools/fetch_cache.py."), *McCache::Data(TEXT("tags/block")));
		return false;
	}
	for (const McData::FFile& File : Files)
	{
		Pending.Add(File.Id, McData::ReadTagValues(File.Paths));
	}
	if (!bHooked)
	{
		// /reload reads them again; a tag keeps its index.
		bHooked = true;
		McData::OnReload().AddLambda([]() { FMcBlockTags::Get().bLoaded = false; FMcBlockTags::Get().EnsureLoaded(); });
	}
	TArray<FString> Names;
	Pending.GetKeys(Names);
	for (const FString& Name : Names)
	{
		TSet<FString> Visiting;
		Resolve(Name, Visiting);
	}
	Pending.Empty();
	bLoaded = true;
	return true;
}

bool FMcBlockTags::Resolve(const FString& Name, TSet<FString>& Visiting)
{
	if (Resolved.Contains(Name))
	{
		return Ids.Contains(Name);
	}
	const TArray<FString>* Values = Pending.Find(Name);
	if (!Values || Visiting.Contains(Name))
	{
		return false;
	}
	Visiting.Add(Name);
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	TBitArray<> Bits(false, Registry.NumBlocks());
	for (const FString& Value : *Values)
	{
		if (Value.StartsWith(TEXT("#")))
		{
			const FString Inner = McCache::Namespaced(Value.RightChop(1));
			if (Resolve(Inner, Visiting))
			{
				Bits.CombineWithBitwiseOR(Members[Ids[Inner]], EBitwiseOperatorFlags::MaintainSize);
			}
		}
		else if (const FMcBlock* Block = Registry.FindBlock(FName(*McCache::Namespaced(Value))))
		{
			Bits[Block->Index] = true;
		}
	}
	Resolved.Add(Name);
	if (const int32* Existing = Ids.Find(Name))
	{
		Members[*Existing] = MoveTemp(Bits);
	}
	else
	{
		Ids.Add(Name, Members.Add(MoveTemp(Bits)));
	}
	return true;
}

int32 FMcBlockTags::Find(const FString& Tag) const
{
	FString Name = Tag;
	Name.RemoveFromStart(TEXT("#"));
	const int32* Found = Ids.Find(McCache::Namespaced(Name));
	return Found ? *Found : INDEX_NONE;
}

bool FMcBlockTags::Has(int32 Tag, FMcStateId State) const
{
	return Members.IsValidIndex(Tag) && HasBlock(Tag, FMcBlockRegistry::Get().GetState(State).Block);
}

bool FMcBlockTags::HasBlock(int32 Tag, int32 BlockIndex) const
{
	return Members.IsValidIndex(Tag) && Members[Tag].IsValidIndex(BlockIndex) && Members[Tag][BlockIndex];
}
