#pragma once
#include "Commandlets/Commandlet.h"
#include "DumpUsmapCommandlet.generated.h"

UCLASS()
class UDumpUsmapCommandlet : public UCommandlet
{
    GENERATED_BODY()
public:
    UDumpUsmapCommandlet();
    virtual int32 Main(const FString& Params) override;
};