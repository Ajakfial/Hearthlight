#pragma once

#include <QString>

namespace Hearthlight {

// Single place to configure Microsoft Azure / Entra authentication.
//
// Out of the box this is Prism Launcher's PUBLIC client ID
// (c36a9fb6-4f2a-41ff-90bd-ae7cc92031eb, published in their CMakeLists and
// reused by several open-source launchers). It needs no secret and lets
// users sign in with zero setup: the device-code flow sends no redirect
// URI, so no per-app registration is required for it.
//
// Three honest caveats:
//   - The Microsoft consent/device-code page shows the registered app name
//     ("Prism Launcher"), not Hearthlight.
//   - By using this key you accept the Microsoft identity platform terms of
//     use (https://docs.microsoft.com/en-us/legal/microsoft-identity-platform/terms-of-use).
//   - If Microsoft ever throttles or revokes shared use, sign-in breaks for
//     everyone on the shared ID until you switch (see below).
//
// Use your own Azure app instead (build-time override, no source edit):
//   cmake --preset linux -DHEARTHLIGHT_MSA_CLIENT_ID=<your-client-id>
// or edit the default below.
//
// How to register your own Azure app (summary, full steps in README.md):
//   1. https://portal.azure.com -> Microsoft Entra ID -> App registrations -> New.
//   2. Supported account types: "Accounts in any organizational directory and
//      personal Microsoft accounts".
//   3. Redirect URI: native client -> msal<CLIENT_ID>://auth (device-code flow
//      needs no redirect, but the portal requires the app to be marked public).
//   4. Enable "Allow public client flows" -> Yes.
//   5. API permissions: Xboxlive.signin + Xboxlive.offline_access (delegated).
//   6. Pass -DHEARTHLIGHT_MSA_CLIENT_ID=<id> at configure time.
#ifdef HEARTHLIGHT_MSA_CLIENT_ID
inline constexpr const char *kAzureClientId = HEARTHLIGHT_MSA_CLIENT_ID;
#else
inline constexpr const char *kAzureClientId = "c36a9fb6-4f2a-41ff-90bd-ae7cc92031eb";
#endif

inline constexpr const char *kAppName = "Hearthlight";
inline constexpr const char *kAppVersion = "0.1.0";
inline constexpr const char *kLauncherName = "Hearthlight";
inline constexpr const char *kOrgName = "Hearthlight";
inline constexpr const char *kOrgDomain = "hearthlight.example";

// Descriptive User-Agent for the Modrinth API.
// Format: hearthlight/<version> (contact)
QString userAgent();

// Mojang endpoints (one definition shared by every layer).
inline constexpr const char *kVersionManifestUrl =
    "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json";

} // namespace Hearthlight
