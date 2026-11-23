#pragma once

namespace tekken8 {

extern void* originalCamera;
extern void* originalActor;
void* cameraHook() noexcept;
void* actorHook() noexcept;

}
