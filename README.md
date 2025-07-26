# CMAA2Plugin
Plugin for UE4.27 - UE5.6 to add support of Conservative Morphological antialiasing v2.0
  
  Based on implementation provided by Intel https://github.com/GameTechDev/CMAA2
## How to use it
Go to Project Settings > Rendering > Default Settings > Anti-Aliasing Method – None
alternatively type in console `r.AntiAliasingMethod 0` 
 For UE 4.27 use `r.DefaultFeature.AntiAliasing 0` or `r.PostProcessAAQuality 0`

Enable CMAA2 with `r.CMAA2.Enable 1`

## CVars
| Name    | Default Value | Description |
| -------- | ------- | ------- |
| r.CMAA2.Enable  | 0    | Enable CMAA2 post-process anti-aliasing. |
| r.CMAA2.Quality | 2     | Sets the quality preset for CMAA2. 0: LOW, 1: MEDIUM, 2: HIGH, 3: ULTRA. |
| r.CMAA2.ExtraSharpness    | 0    | Set to 1 to preserve more text and shape clarity at the expense of less AA. |
| r.CMAA2.Debug | 0 | Set to 1 to enable debug visualization of detected edges. |

## Tested engine versions
  ✅ Unreal Engine 4.27.2  
  ✅ Unreal Engine 5.0.3  
  ✅ Unreal Engine 5.1.3  
  ✅ Unreal Engine 5.2.1  
  ✅ Unreal Engine 5.3.2  
  ✅ Unreal Engine 5.4.4  
  ✅ Unreal Engine 5.5.4  
  ✅ Unreal Engine 5.6.0   
  ❓ Should probaly work on every patch in between, untested
