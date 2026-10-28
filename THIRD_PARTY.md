# Third-party code and licensing

DeskNX's original standalone UI/model code is licensed under the repository's MIT license unless a file says otherwise.

## sLaunch

The experimental HOME/system-backend components under `projects/sSystem/` include adaptations from **sLaunch** by etonedemid, upstream repository: https://github.com/etonedemid/slaunch . The upstream project is distributed under GNU GPL version 3 or later; the upstream license notice is preserved in `projects/sSystem/COPYING.sLaunch`.

Adapted areas currently include:
- Installed-title launch lifecycle and save-data preparation in `projects/sSystem/source/Application.cpp` and `projects/sSystem/include/desknx/system/Application.hpp`.
- Atmosphère External Content Storage (ECS) filesystem registration/serving in `projects/sSystem/source/ExternalContent.cpp` and `projects/sSystem/include/desknx/system/ExternalContent.hpp`.
- The initial applet-slot NPDM configurations under `projects/sSystem/DeskNXSystem.json` and `projects/DeskNXMenu/DeskNXMenu.json`, based on upstream sLaunch configurations.

These adaptations retain the architecture and notices needed to understand their origins. The ECS approach in sLaunch itself cites uLaunch (Xortroll and contributors, GPLv2) as an upstream source; see the comments inside `ExternalContent.cpp`. Consult both upstream projects' licensing before redistributing a combined build.

The standalone DeskNX NRO build is separate from the system backend and does not install a qlaunch override. The optional system backend is experimental and has not been validated on a physical console. Do not treat the current source tree or generated build package as a production-ready HOME replacement.
