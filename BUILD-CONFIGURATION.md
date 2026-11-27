# Moonlight build configuration

One build-time switch selects the product environment: `LG_STAGING_BUILD`.

| Setting | Staging (`LG_STAGING_BUILD`) | Production (default) |
|---|---|---|
| App API base | `https://django-stg.legiongames.cell/app/v1` | `https://legiongames.ru/app/v1` |
| NetBird management | staging management host | `netbird.legiongames.ru` |
| Vault/DPAPI namespace | `ru.legiongames.moonlight.staging` | production namespace |
| Deep-link names | staging deep-link names | production deep-link names |
| Update channel | staging | production |
| Update checks | disabled in staging builds | enabled |

Rules:

- `LG_API_BASE_URL` is a local/test override only. It must not be set in a
  shipped build; a non-HTTPS URL is rejected by `AccountClient`.
- Staging must never read production vault entries, registry backups,
  Keychain items, libsecret entries, update manifests, or NetBird profiles.
- Production must never point at `*.legiongames.cell`.
- The Windows MSI ProductVersion is independent of the environment and uses
  `6.2.<CI run>` for a real MajorUpgrade.
- Installer and update artifacts must be built from the same commit and the
  same `LG_STAGING_BUILD` value as the app binary.

Dependency ownership:

- Qt: pinned by the CI matrix; do not mix Qt 5 and Qt 6 behavior in one
  installer.
- OpenSSL: pinned with the Qt/CI image; no system-library fallback in release.
- FFmpeg/SDL: pinned by the build scripts and submodules.
- CI actions: pin major versions and review deprecation notices.

A release is invalid if staging and production artifacts share a vault
namespace, API base, update channel, or signing identity.
