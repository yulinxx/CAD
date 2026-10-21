# __PRODUCT_NAME__ client plugin

This repository is a per-client white-label extension for SanYi CAD.

## Layout

- `configs/__CLIENT_ID__.json` — menu / dock / status bar overrides, `extends: base`
- `branding/__CLIENT_ID__/brand.json` — product name, company, icon, theme
- `src/ClientPlugin.cpp` — panel plugin (`createClientUiPlugin`)

## Build

```powershell
cmake --preset default
cmake --build build --config Release
```

The plugin is written to:

```text
<build>/bin_Qt6/Release/plugins/clients/__CLIENT_ID__/
```

Config and branding are copied next to the executable automatically.

## Package

From the core repository:

```powershell
./Tools/PackageClient.ps1 -ClientId __CLIENT_ID__
```

## Notes

- Keep `clientId` identical in `configs/`, `branding/`, and `ClientPlugin.cpp`.
- Do not change `IClientUiPlugin.h`; it is the ABI contract with the host.
- Rebuild the plugin whenever the host Qt/MSVC toolchain changes.
