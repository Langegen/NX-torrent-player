#pragma once

namespace osk
{

// Applies config::get().builtinKeyboard, switching Borealis' IME manager
// between the platform keyboard (e.g. Switch swkbd) and the built-in OSK.
void applyConfig();

} // namespace osk
