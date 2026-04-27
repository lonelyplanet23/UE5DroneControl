#include "DroneRuntimeInteractionGameMode.h"

#include "DroneRuntimeInteractionPlayerController.h"
#include "GameFramework/SpectatorPawn.h"

ADroneRuntimeInteractionGameMode::ADroneRuntimeInteractionGameMode()
{
	PlayerControllerClass = ADroneRuntimeInteractionPlayerController::StaticClass();
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}
