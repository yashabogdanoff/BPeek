#pragma once
#include "Commandlets/Commandlet.h"
#include "BPeekScanCommandlet.generated.h"

UCLASS()
class BPEEK_API UBPeekScanCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UBPeekScanCommandlet();
    virtual int32 Main(const FString& Params) override;
};