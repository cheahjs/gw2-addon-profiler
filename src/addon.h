#pragma once

#include <windows.h>
#include <Nexus.h>

namespace Addon {

extern HMODULE g_hModule;
extern AddonAPI_t* API;

void Load(AddonAPI_t* api);
void Unload();

} // namespace Addon
