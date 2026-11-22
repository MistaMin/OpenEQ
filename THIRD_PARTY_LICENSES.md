# Third-party licenses

OpenEQ's own source code is MIT licensed (see `LICENSE`). Official complete binaries are proprietary
freeware under the publisher's commercial JUCE licence (see `BINARY_LICENSE.txt`), not a relicensing of
JUCE or other dependencies under MIT. End users need no JUCE developer licence to use these official
downloads. Independent builders must satisfy the JUCE licence applicable to their builds.

| Component | Used for | License |
|---|---|---|
| JUCE 8.0.6 | plugin framework, user interface, plugin formats | AGPLv3 or the commercial JUCE licence. OpenGrid builds its released binaries under its own JUCE licence. Anyone building from source needs a JUCE licence that covers their use and distribution. |
| GoodLookinUI (`third_party/GoodLookinUI`) | knobs, keys, lamps, meters | MIT |
| VST3 SDK (bundled with JUCE) | VST3 format | MIT |
| Audio Unit SDK (bundled with JUCE, macOS only) | Audio Unit format | Apache License 2.0 |
| zlib, libpng, HarfBuzz, libwebp, lunasvg, SheenBidi and audio codecs (bundled with JUCE) | JUCE internals | their own licenses, included in the JUCE source |

The open-source licence for JUCE 8 is **AGPLv3**, not plain GPL. A commercial JUCE licence is the
alternative. See the official licence at https://juce.com. The publisher's commercial licence is not
transferred with the MIT source.

VST is a trademark of Steinberg Media Technologies GmbH. Audio Units is a trademark of Apple Inc.
AAX and Pro Tools are trademarks of Avid Technology, Inc. Other format names are used only to describe
compatibility.

| Component | Use | Licence |
|---|---|---|
| AAX SDK (Avid) | AAX format, build time only; not part of this repository | Avid AAX Developer Agreement / SDK License |
