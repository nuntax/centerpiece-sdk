#include "SkinCreatorLibrary.h"

static UKeyEventReceiver* GKeyEventReceiver = nullptr;

UKeyEventReceiver* USkinCreatorLibrary::GetKeyEventReceiver()
{
    return GKeyEventReceiver;
}

void USkinCreatorLibrary::SetKeyEventReceiver(UKeyEventReceiver* Receiver)
{
    GKeyEventReceiver = Receiver;
    if (GKeyEventReceiver)
    {
        GKeyEventReceiver->AddToRoot();
    }
}

FVector2D USkinCreatorLibrary::GetPositionByKeyIndex(uint8 KeyIndex)
{
    return FVector2D::ZeroVector;
}
