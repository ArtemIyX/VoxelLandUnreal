#pragma once

#include "CoreMinimal.h"
#include "Data/Structs/VoxelLandValueSource.h"

struct FCanonicalWriter
{
	TArray<uint8> Bytes;
	void U8(uint8 V) { Bytes.Add(V); }
	void U16(uint16 V)
	{
		U8(uint8(V));
		U8(uint8(V >> 8));
	}
	void U32(uint32 V)
	{
		for (int32 ByteIndex = 0; ByteIndex < 4; ++ByteIndex)
		{
			U8(uint8(V >> (ByteIndex * 8)));
		}
	}
	void Float(float V)
	{
		uint32 Bits = 0;
		if (V != 0.0f)
		{
			FMemory::Memcpy(&Bits, &V, sizeof(Bits));
		}
		U32(Bits);
	}
	void String(const FString& S)
	{
		FTCHARToUTF8 Utf8(*S);
		U32(uint32(Utf8.Length()));
		Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	}
	void Source(const FVoxelLandValueSource& S)
	{
		U8(S.bParameter ? 1 : 0);
		U32(S.Index);
	}
};
