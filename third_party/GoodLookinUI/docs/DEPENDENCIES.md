# Dependencies and licensing

GoodLookinUI 0.2.3 is header-only C++20 source under the MIT License.
The design/CSV/motion core includes only C++ standard library headers.
It does not require JUCE, a plugin SDK, a graphics runtime or a separate font.

The optional JUCE adapter includes `juce_gui_basics` and uses JUCE's components,
graphics and file chooser. Its own code is MIT licensed, but JUCE is separately
licensed. It is supplied by the host project and is not redistributed here.
The adapter has been verified against JUCE 8.0.4 on Apple Silicon/macOS.

Projects using JUCE must follow the applicable JUCE commercial or open-source
terms. GoodLookinUI's MIT license does not grant a JUCE license and does not
transfer the author's commercial license. Choosing JUCE's open-source route
requires compliance with its corresponding copyleft terms; choosing an eligible
commercial license allows distribution under that agreement's terms.
Official JUCE 8 terms: https://juce.com/legal/juce-8-licence/
JUCE source licensing: https://github.com/juce-framework/JUCE/blob/8.0.4/LICENSE.md

Clang, GCC, MSVC and CMake are development tools, not bundled toolkit components.
Their licenses are not additional GoodLookinUI source licenses. Toolchain/runtime
redistribution obligations still apply to whatever a final product includes.

No downloaded textures, sampled knob images, third-party fonts or icon packs
are part of this repository. Knobs, panels, keys, screws and shading are generated
in code. Plugin-format SDKs are the plugin project's responsibility, not core
dependencies of this toolkit.

Keep the copyright and MIT permission notice with copies or substantial portions
of GoodLookinUI, including compiled distributions. Include LICENSE with source
copies and an MIT notice in the final application's notices or distribution.
You may use GoodLookinUI in commercial and proprietary products under MIT;
those products must separately satisfy licenses for other code they include.
