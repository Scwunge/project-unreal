// Chunks as bytes, for sending to clients: each section either one state or 4096 of them, then zlib.

#pragma once

#include "CoreMinimal.h"

class FMcChunk;

namespace McChunkCodec
{
	MCNET_API TArray<uint8> Encode(const FMcChunk& Chunk);

	// Fills Chunk (already made for the right position and height) from Encode's bytes. False if the bytes
	// are damaged or for a chunk of another height; Chunk may then be partly filled.
	MCNET_API bool Decode(TConstArrayView<uint8> Bytes, FMcChunk& Chunk);
}
