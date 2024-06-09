# <img src="https://sk-data.special-k.info/artwork/strangorth/24.png" width="24" alt="Animated eclipse icon for Special K Image Viewer (SKIV)"> Special K Image Viewer (SKIV)
![Screenshot of the app](https://sk-data.special-k.info/artwork/screens/skiv_initial.png)

A companion image viewer for the [Special K Injection Frontend](https://github.com/SpecialKO/SKIF) (SKIF). Can also be used separately.

New versions will probably be distributed through their own packaged installer.

## Features

- Basic image viewer
- HDR support
- Drag-n-drop support

## Command line arguments

| Argument&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| -----------------------------: | -------------- |
| `<empty>`                      | Launches the app. |
| `Exit`                         | Closes all running instances of the app. |
| `Minimize`                     | Launches SKIV minimized *or* minimizes any running instances of SKIV. |
| `"<path-to-local-image-file>"` | Opens the provided image in the app. |

## Keyboard shortcuts

| Shortcut&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `Ctrl+O` *or* `Ctrl+A`    | Open a new image.                                       |
| `Ctrl+D`                  | Toggle image details.                                   |
| `F1` *or* `Ctrl+1`        | Switch to the Viewer tab.                               |
| `F2` *or* `Ctrl+2`        | Switch to the Settings tab.                             |
| `F6`                      | Appearance: Toggles DPI scaling.                        |
| `F7`                      | Appearance: Cycles between available color themes.      |
| `F8`                      | Appearance: Toggles borders.                            |
| `F9`                      | Appearance: Toggles color depth.                        |
| `F11` *or* `Ctrl+F`       | Toggle fullscreen mode.                                 |
| `Esc` *or* `Ctrl+Q` *or* `Ctrl+W` | Closes the app.                                 |
| `Ctrl+N`                  | Minimizes the app.                                      |

## Third-party code

* Uses [Dear ImGui](https://github.com/ocornut/imgui), licensed under [MIT](https://github.com/ocornut/imgui/blob/master/LICENSE.txt).
* Uses [DirectX Texture Library](http://go.microsoft.com/fwlink/?LinkId=248926), licensed under [MIT](https://github.com/microsoft/DirectXTex/blob/main/LICENSE).
* Uses [Font Awesome Free v6.2.1](https://fontawesome.com/v6/download), licensed under [SIL OFL 1.1 License](https://scripts.sil.org/OFL).
* Uses [JSON for Modern C++](https://github.com/nlohmann/json), licensed under [MIT](https://github.com/nlohmann/json/blob/develop/LICENSE.MIT).
* Uses [PicoSHA2](https://github.com/okdshin/PicoSHA2), licensed under [MIT](https://github.com/okdshin/PicoSHA2/blob/master/LICENSE).
* Uses [Plog](https://github.com/SergiusTheBest/plog), licensed under [MIT](https://github.com/SergiusTheBest/plog/blob/master/LICENSE).
* Uses [pugixml](https://pugixml.org/), licensed under [MIT](https://pugixml.org/license.html).
* Uses [ValveFileVDF](https://github.com/TinyTinni/ValveFileVDF), licensed under [MIT](https://github.com/TinyTinni/ValveFileVDF/blob/master/LICENSE).
* Uses [TextFlowCpp](https://github.com/catchorg/textflowcpp), licensed under [BSL-1.0](https://github.com/catchorg/textflowcpp/blob/master/LICENSE.txt).
* Uses [HybridDetect](https://github.com/GameTechDev/HybridDetect/), licensed under [MIT](https://github.com/GameTechDev/HybridDetect/blob/main/LICENSE.md).
* Includes various snippets of code from [Stack Overflow](https://stackoverflow.com/), licensed under [Creative Commons Attribution-ShareAlike](https://stackoverflow.com/help/licensing).
