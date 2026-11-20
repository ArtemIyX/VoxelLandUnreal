#pragma once
#include "CoreMinimal.h"

namespace VoxelLandPublication
{
    // The caller first looks up the record by the completion's chunk key.
    inline bool IsCurrent(bool bRecordExists, uint64 CompletionEpoch, uint64 CurrentEpoch,
        uint64 CompletionTicket, uint64 RecordTicket, bool bSameGenerator, bool bWanted)
    {
        return bRecordExists && CompletionEpoch==CurrentEpoch && CompletionTicket==RecordTicket && bSameGenerator && bWanted;
    }
}
