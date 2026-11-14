# Integration guide

## Core only

Add GoodLookinUI to your project and link its interface target:

```cmake
add_subdirectory(path/to/GoodLookinUI)
target_link_libraries(MyTarget PRIVATE goodlookinui)
```

Include `<goodlookinui/Design.h>`. No compiled library or JUCE setup is required.
The project needs a C++20 compiler; CMake configuration requires CMake 3.22+.

```cpp
#include <goodlookinui/Design.h>
#include <sstream>

goodlookinui::Item gain{"input-gain", "inputGain", "Input", "console", "#B69B73",
                       12, 52, 150, 98, 11};
std::stringstream design;
goodlookinui::writeDesign(design, {gain});
auto items = goodlookinui::readDesign(design);
```

## JUCE renderer

Add your licensed JUCE checkout before adding GoodLookinUI so that the adapter
can find the `juce::juce_gui_basics` target:

```cmake
add_subdirectory(path/to/JUCE)
add_subdirectory(path/to/GoodLookinUI)
target_link_libraries(MyPlugin PRIVATE goodlookinui_juce)
target_compile_definitions(MyPlugin PRIVATE
    GOODLOOKINUI_ENABLE_EDITOR=$<CONFIG:Debug>)
```

Include `<GoodLookinUI.h>` in your JUCE component. In `paint`, call:

```cpp
goodlookinui::juce_adapter::drawKnob(g, knobBounds, normalizedValue, gain, true);
```

`knobBounds` is a `juce::Rectangle<float>`; `normalizedValue` is in [0,1].
Supported styles: `metal`, `bakelite`, `ivory`, `console`. Helpers also draw
panels, keys and screws. These are drawing helpers, not parameter attachments.
The host plugin implements slider interaction and DSP/host automation bindings.
Plugins that use the toolkit live in their own repositories; none is included here.

For animation, step a `goodlookinui::Motion` on the UI thread with elapsed seconds.
Send parameter changes directly to DSP; only animate the displayed position.
Do not read/write CSV or use the inspector on the audio processing thread.

## Development inspector

Compile `GOODLOOKINUI_ENABLE_EDITOR=1` only for development builds. Under that
same preprocessor guard, own a `goodlookinui::juce_adapter::Studio`, attach it to
your editor and give it bounds. Register controls with
`studio.add(item, component, applyCallback)`. The callback applies the changed
`Item` to your component's rendering properties. Registered components must
outlive their Studio entries. The inspector does not own DSP parameter bindings.

Select a registered control to change style, label, colour, font and bounds.
Apply, Undo, Save CSV and Load CSV are supported. The host should compile out
both Studio and its launch button in Release builds. Embed the accepted CSV
in your plugin build; release users do not need editable files on disk.

## Design file format

The first line is `GoodLookinUI design version 1`. Columns are:
`id,parameter,label,style,colour,x,y,width,height,font size`.
Use unique IDs, single-line text and `#RRGGBB` colours. CSV supports quoted commas
and escaped quotes. The loader validates style, geometry, numbers and duplicate
IDs; failures throw exceptions. Coordinates are relative to the control's parent.
The original `HardwareUI design version 1` header remains readable.

This release is an initial foundation. Drag/drop control creation, menu editing,
additional GUI backends and cross-platform qualification are future work; see
IMPLEMENTATION.md. Existing FFT/DSP displays belong to the host plugin.
