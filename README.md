# <img src="https://sk-data.special-k.info/artwork/strangorth/24.png" width="24" alt="Animated eclipse icon for Special K Image Viewer (SKIV)"> Special K Image Viewer (SKIV)
![Screenshot of the app](https://sk-data.special-k.info/artwork/screens/skiv_initial.png)

An experimental companion image viewer for the [Special K Injection Frontend](https://github.com/SpecialKO/SKIF) (SKIF).
Is also intended to be able to function separately, although not all planned features may be available when used in such a form.

The intention is to build a simple yet advanced screenshot/image viewer tool that handles HDR images properly as well as function as a testbed for features and functionality for the main Special K project.

New versions will probably be distributed through their own packaged installer once the project reaches more maturity.

## Features

- Quick and simple image viewer
- HDR support
- HDR visualization
- Drag-n-drop support of both local and internet image links
- Copy/paste support
- Desktop/region screenshot capture

## Format support

* JXR
* JXL* (Requires Special K)
* AVIF* (Windows 11 only)
* Radiance HDR (.hdr)
* PNG* (+ HDR support)
* JPEG (JPG)
* WebP*
* PSD
* GIF*
* BMP
* TIFF

*\* No animation support.*

## Command line arguments

| Argument&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------------: | -------------- |
| `<empty>`                       | Launches the app.                         |
| `"<path-to-local-image-file>"`  | Opens the provided image path in the app. |
| `"<link-to-online-image-file>"` | Opens the provided image link in the app. |
| `/OpenFileDialog`               | Open the file dialog of the app.          |
| `/Exit`                         | Closes all running instances of the app.  |

## Keyboard shortcuts

| Shortcut&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp;&ensp; | What it does |
| ------------------------: | -------------- |
| `Ctrl+A` *or* `Ctrl+O`    | Open a new image.                                       |
| `Ctrl+D`                  | Toggle image details.                                   |
| `Ctrl+1`                  | Image Scaling: View actual size (None / 1:1)            |
| `Ctrl+2`*or* `Ctrl+0`     | Image Scaling: Zoom to fit (Fit)                        |
| `Ctrl+3`                  | Image Scaling: Fill the window (Fill)                   |
| `Ctrl+W`                  | Close the currently opened image                        |
| `Ctrl+E`                  | Browse folder / Open in File Explorer                   |
| `Ctrl+Windows+Shift+P`    | Capture a screenshot of a region of the display.        |
| `Ctrl+Windows+Shift+O`    | Capture a screenshot of the display.                    |
| `F1`                      | Switch to the Viewer tab.                               |
| `F2`                      | Switch to the Settings tab.                             |
| `F6`                      | Appearance: Toggles DPI scaling.                        |
| `F7`                      | Appearance: Cycles between available color themes.      |
| `F8`                      | Appearance: Toggles borders.                            |
| `F9`                      | Appearance: Toggles color depth.                        |
| `F11` *or* `Ctrl+F`       | Toggle fullscreen mode.                                 |
| `Esc` *or* `Ctrl+Q`       | Closes the app.                                         |
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
* Optionally Uses [libjxl](https://github.com/libjxl/), licensed under [BSD](https://raw.githubusercontent.com/libjxl/libjxl/main/LICENSE).
* Includes various snippets of code from [Stack Overflow](https://stackoverflow.com/), licensed under [Creative Commons Attribution-ShareAlike](https://stackoverflow.com/help/licensing).
