# Aetherion — Game Engine

<p align="center">
  <img src="misc/logo/aetherion/aetherion-core-256.png" width="220" alt="Aetherion Core emblem">
</p>

<p align="center">
  <em>A custom <a href="https://godotengine.org">Godot Engine</a> distribution by Rogue Fairy Studios.</em>
</p>

---

**Aetherion** is an open-source 2D and 3D game engine distribution maintained by **Rogue Fairy Studios** on top of **[Godot Engine](https://godotengine.org)** (`4.8`), tailored for narrative-driven and adventure games and studio production pipelines.

> **Upstream Attribution & Non-Endorsement Notice:**  
> Aetherion is proudly built upon [Godot Engine](https://github.com/godotengine/godot) and distributed under the same [MIT license](LICENSE.txt). Aetherion is an independent distribution by Rogue Fairy Studios and is **not** an official product of, affiliated with, or endorsed by the [Godot Foundation](https://godot.foundation/). Exported games do not impose Aetherion branding onto user projects.

## Highlights on the `aetherion` Branch

- **Arcano Tecnológico Interface (`Aetherion Obsidian`):** High-contrast Obsidian (`#0B0C10`), Charcoal (`#16181D`), and Arcane Gold (`#D6B77A`) default editor theme preset alongside classic Godot presets.
- **Native OpenID Connect / Authentik Module (`modules/authentik/`):** Built-in OAuth 2.0 Authorization Code + PKCE (`S256`) and Device Authorization Grant (RFC 8628) client, AES-256 encrypted session storage, Editor/Project Manager account integration, and `StudioApiClient` workspace browser (with zero hardcoded organization URLs or client IDs).
- **Isolated Editor Configuration:** Stores editor settings and sessions in its own `Aetherion` configuration directory so it can coexist side-by-side with stock Godot installations.

## Building from Source

Active Aetherion development lives on the **`aetherion`** branch (`master` tracks upstream `godotengine/godot`). Building requires **Python 3.10+** and **SCons 4.4+**:

```bash
git clone --branch aetherion https://github.com/RogueFairyStudios/aetherion.git
cd aetherion
python -m pip install scons

# Build the editor (use platform=windows, linuxbsd, or macos)
scons platform=windows target=editor
```

Editor binaries are written to `bin/aetherion.<platform>.editor.<arch>[.exe]`. Export template builds (`target=template_debug` / `target=template_release`) retain standard `bin/godot.*` naming and neutral runtime defaults.

Platform-specific compiler prerequisites are documented in the [Godot Engine compilation guide](https://docs.godotengine.org/en/latest/engine_details/development/compiling/).

## License & Credits

- **Aetherion additions & branding:** © Rogue Fairy Studios (code distributed under the [MIT license](LICENSE.txt)).
- **Godot Engine:** © 2014-present [Godot Engine contributors](AUTHORS.md) · © 2007-2014 Juan Linietsky, Ariel Manzur ([MIT license](LICENSE.txt)).
- Third-party component notices are listed in [COPYRIGHT.txt](COPYRIGHT.txt).
