#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include "ship/Context.h"
#include "ship/config/Config.h"

namespace Ship {
ConnectedPhysicalDeviceManager::ConnectedPhysicalDeviceManager() {
}

ConnectedPhysicalDeviceManager::~ConnectedPhysicalDeviceManager() {
}

std::unordered_map<int32_t, SDL_GameController*>
ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadsForPort(uint8_t portIndex) {
    std::unordered_map<int32_t, SDL_GameController*> result;

    for (const auto& [instanceId, gamepad] : mConnectedSDLGamepads) {
        if (!PortIsIgnoringInstanceId(portIndex, instanceId)) {
            result[instanceId] = gamepad;
        }
    }

    return result;
}

std::unordered_map<int32_t, std::string> ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadNames() {
    return mConnectedSDLGamepadNames;
}

std::unordered_set<int32_t> ConnectedPhysicalDeviceManager::GetIgnoredInstanceIdsForPort(uint8_t portIndex) {
    return mIgnoredInstanceIds[portIndex];
}

bool ConnectedPhysicalDeviceManager::PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId) {
    return GetIgnoredInstanceIdsForPort(portIndex).contains(instanceId);
}

void ConnectedPhysicalDeviceManager::IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].insert(instanceId);
}

void ConnectedPhysicalDeviceManager::UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].erase(instanceId);
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceConnect(int32_t sdlDeviceIndex) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceDisconnect(int32_t sdlJoystickInstanceId) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::RefreshConnectedSDLGamepads() {
    mConnectedSDLGamepads.clear();
    mConnectedSDLGamepadNames.clear();

    // Two-instance local co-op: pin port 1 to a single physical pad by its
    // gamepad enumeration ordinal, so each game instance can own one pad.
    // -1 (default) keeps stock behavior: every pad drives port 1.
    int32_t port0Ordinal = -1;
    auto config = Context::GetInstance() != nullptr ? Context::GetInstance()->GetConfig() : nullptr;
    if (config != nullptr) {
        port0Ordinal = config->GetInt("Controllers.Port0GamepadOrdinal", -1);
    }

    // Rebuild port 0's ignore set from scratch: instance ids change on every
    // replug, and stale entries would leave the rightful pad ignored after a
    // hotplug shifts the ordinals (Bluetooth pad sleep is routine).
    if (port0Ordinal >= 0) {
        mIgnoredInstanceIds[0].clear();
    }

    int32_t gamepadOrdinal = 0;
    for (int32_t i = 0; i < SDL_NumJoysticks(); i++) {
        // skip if this SDL joystick isn't a Gamepad
        if (!SDL_IsGameController(i)) {
            continue;
        }

        auto gamepad = SDL_GameControllerOpen(i);
        auto instanceId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad));
        auto name = SDL_GameControllerName(gamepad);

        mConnectedSDLGamepads[instanceId] = gamepad;
        mConnectedSDLGamepadNames[instanceId] = name;

        if (port0Ordinal >= 0 && gamepadOrdinal != port0Ordinal) {
            mIgnoredInstanceIds[0].insert(instanceId);
        }

        for (uint8_t port = 1; port < 4; port++) {
            mIgnoredInstanceIds[port].insert(instanceId);
        }

        gamepadOrdinal++;
    }
}
} // namespace Ship
