# CMAA2 Plugin for Unreal Engine
Integrates Intel's Conservative Morphological Anti-Aliasing v2.0 (CMAA2) as a post-processing anti-aliasing method for Unreal Engine versions 4.27 up to 5.6.  
  
  This plugin provides a high-performance, high-quality, non-temporal anti-aliasing solution, making it an excellent alternative to TAA for projects where temporal stability is a lower priority than raw performance and sharpness.

  
  This implementation is based on the official source code provided by Intel: https://github.com/GameTechDev/CMAA2
  
## Features
- **High-Quality Anti-Aliasing**: Implements the full CMAA2 algorithm for effective edge smoothing.
- **High Performance**: Offers a significant performance improvement over temporal anti-aliasing methods.
- **Configurable Quality**: Comes with four distinct quality presets (Low, Medium, High, Ultra) to balance performance and visual fidelity.
- **Sharpness Control**: Includes an option to enhance sharpness, preserving fine details in textures and text.
- **Debug Visualization**: A built-in debug mode allows you to visualize the edges detected by the algorithm in real-time.
- **Wide Engine Support**: Compatible with a broad range of Unreal Engine versions, from UE 4.27 to the latest (5.6).

## Installation
 1. Create a Plugins folder in your project's root directory (e.g., `MyProject/Plugins/`) if it doesn't already exist
 2. Download the ZIP file or clone this repository into `Plugins` folder.
 3. Extract the contents of the downloaded ZIP file into the Plugins folder. The final path should look like `MyProject/Plugins/CMAA2Plugin/`
 4. Restart your project.
 5. The editor will prompt you to rebuild the missing modules. Click Yes.

## Usage
  To enable and use CMAA2, follow these steps:
  1. **Enable the Plugin**: In the Unreal Editor, go to `Edit > Plugins` and ensure the "CMAA2 Anti-Aliasing" plugin is enabled under the "Rendering" category.
  2. **Disable Native AA**: CMAA2 can only function when the engine's built-in anti-aliasing is disabled.
    - Go to `Project Settings > Engine > Rendering`.
    - Under the `Default Settings` category, set the **Anti-Aliasing Method** to **None**.
  3. **Enable CMAA2**: Open the console (using the ~ key) and enter the following command:
```
r.CMAA2.Enable 1
```
  
  To make this setting persistent, you can add the command to your project's configuration file. Open `Config/DefaultEngine.ini` and add the following lines:
```
[SystemSettings]
r.AntiAliasingMethod=0
r.CMAA2.Enable=1
```

## Configuration
CMAA2 can be configured at runtime using the following console variables:   
| Console Variable    | Description | Values | Default |
| -------- | ------- | -------- | ------- |
| `r.CMAA2.Enable`    | Globally enables or disables the CMAA2 effect. Remember, r.AntiAliasingMethod must be 0 for this to work. | 0: Disabled<br> 1: Enabled | 1 |  
| `r.CMAA2.Quality`   | Adjusts the quality preset. Higher presets improve edge detection and smoothing at a minor performance cost. | 0: Low<br> 1: Medium<br> 2: High<br> 3: Ultra | 2 |  
| `r.CMAA2.ExtraSharpness`  | Increases the sharpness of the final image, preserving more detail at the expense of less aliasing reduction. | 0: Disabled<br>1: Enabled | 0 |  
| `r.CMAA2.Debug`    | Toggles a debug view that overlays the detected edges on the screen, helping to tune quality settings. | 0: Disabled<br>1: Enabled | 0 |  

Additionally you can balance CMAA2 quality and performance by adjusting the `CMAA2_MAX_LINE_LENGTH` inside `CMAA2PostProcess.cpp`


## Tested engine versions
  ✅ Unreal Engine 4.27.2  
  ✅ Unreal Engine 5.0.3  
  ✅ Unreal Engine 5.1.3  
  ✅ Unreal Engine 5.2.1  
  ✅ Unreal Engine 5.3.2  
  ✅ Unreal Engine 5.4.4  
  ✅ Unreal Engine 5.5.4  
  ✅ Unreal Engine 5.6.0   
  ❓ Should probably work on every patch in between, untested


# Credits and License
- Plugin Author: Maksym Paziuk and contributors.
- Original Algorithm: This plugin is based on the [official CMAA2 implementation by Intel Corporation](https://github.com/GameTechDev/CMAA2).  
- This plugin is released under the [MIT License](https://opensource.org/license/mit). See the LICENSE file for more details.   
- The original CMAA2 source code from Intel is licensed under the [Apache License, Version 2.0](https://www.apache.org/licenses/LICENSE-2.0). 