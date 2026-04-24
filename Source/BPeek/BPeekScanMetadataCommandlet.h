#pragma once
#include "Commandlets/Commandlet.h"
#include "BPeekScanMetadataCommandlet.generated.h"

UCLASS()
class UBPeekScanMetadataCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UBPeekScanMetadataCommandlet();
    virtual int32 Main(const FString& Params) override;
};