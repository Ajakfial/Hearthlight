#pragma once

#include <QString>

namespace Hearthlight {

// Single place to configure Microsoft Azure / Entra authentication.
//
// How to register your own Azure app (summary, full steps in README.md):
//   1. https://portal.azure.com -> Microsoft Entra ID -> App registrations -> New.
//   2. Supported account types: "Accounts in any organizational directory and
//      personal Microsoft accounts".
//   3. Redirect URI: native client -> msal<CLIENT_ID>://auth (device-code flow
//      needs no redirect, but the portal requires the app to be marked public).
//   4. Enable "Allow public client flows" -> Yes.
//   5. API permissions: Xboxlive.signin + Xboxlive.offline_access (delegated).
//   6. Copy the Application (client) ID into kAzureClientId below.
inline constexpr const char *kAzureClientId = "00000000-0000-0000-0000-000000000000";

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
