#include "McBlockTags.h"
#include "HAL/FileManager.h"
#include "McBlockRegistry.h"
#include "McCache.h"
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
	// Tags can sit in subfolders (e.g. mineable/pickaxe); their names keep the folder.
	const FString Root = McCache::Data(TEXT("tags/block/"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.json"), true, false);
	if (Files.IsEmpty())
	{
		UE_LOG(LogMcCore, Error, TEXT("No block tags in %s; run tools/fetch_cache.py."), *Root);
		return false;
	}
	for (const FString& File : Files)
	{
		FString Relative = File;
		FPaths::MakePathRelativeTo(Relative, *Root);
		Relative = FPaths::ChangeExtension(Relative, TEXT("")).Replace(TEXT("\\"), TEXT("/"));
		TArray<FString> Values;
		if (const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(File))
		{
			// Entries are names, or { "id": name, "required": false } for optional ones.
			const TArray<TSharedPtr<FJsonValue>>* List;
			if (Json->TryGetArrayField(TEXT("values"), List))
			{
				for (const TSharedPtr<FJsonValue>& Value : *List)
				{
					FString Entry;
					if (Value->TryGetString(Entry) || (Value->Type == EJson::Object && Value->AsObject()->TryGetStringField(TEXT("id"), Entry)))
					{
						Values.Add(Entry);
					}
				}
			}
		}
		Pending.Add(McCache::Namespaced(Relative), MoveTemp(Values));
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
	if (Ids.Contains(Name))
	{
		return true;
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
	Ids.Add(Name, Members.Add(MoveTemp(Bits)));
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
