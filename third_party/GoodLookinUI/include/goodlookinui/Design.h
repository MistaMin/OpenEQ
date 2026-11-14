// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <limits>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace goodlookinui {
// Knob classes. The short code is what appears in design files and in code.
// For capped styles (Brit, N, A, FS, Rd) the item colour is the cap colour;
// for material styles (Mtl, Wd, Bk, Pie, Snk, Slv, Ptr) it is the indicator colour.
// Digital styles (Seg, Vfd, Cyb) and Dm/Sq use it as the lit / glow / cap colour.
enum class KnobStyle { Brit, N, A, FS, Mtl, Wd, Bk, Pie, Snk, Slv, Rd, Ptr, Dm, Sq, Seg, Vfd, Cyb,
                       Fat, Pag, Lvr, Tab, Wh, Knl, Skt, Rib, Chr, Arc, Brg, Neo, Hex, Yel, Key, Brz, Chk, Ivr, Mpl, Led, Dbl, Mini, Tl, Glw, Grd, Dsh, Mic, Ebn, Wnt };
struct StyleInfo { KnobStyle style; const char* code; const char* legacy; const char* description; };
inline constexpr StyleInfo knobStyles[] = {
    {KnobStyle::Brit,"Brit","console","Black fluted grip, large coloured flat cap, recessed white pointer"},
    {KnobStyle::N,   "N",   "",       "Chrome collar, grey fluted grip, flat cap, dotted scale ring"},
    {KnobStyle::A,   "A",   "",       "Flared silver crown with a large flat matte cap (any colour, dark for charcoal), dotted ring"},
    {KnobStyle::FS,  "FS",  "",       "Pale-grey skirt, coloured cap, black pointer line"},
    {KnobStyle::Mtl, "Mtl", "metal",  "Knurled aluminium body, brushed top, dark pointer line"},
    {KnobStyle::Wd,  "Wd",  "",       "Turned wooden knob, grain, inlaid pointer line"},
    {KnobStyle::Bk,  "Bk",  "bakelite","Black bakelite teardrop pointer over a ribbed base"},
    {KnobStyle::Pie, "Pie", "",       "White serrated pie-crust skirt, black cap, pointer line"},
    {KnobStyle::Snk, "Snk", "",       "Matte black cylinder, fixed tick ring, pointer line"},
    {KnobStyle::Slv, "Slv", "ivory",  "Silver-grey cylinder, dark skirt ring with dots, black pointer"},
    {KnobStyle::Rd,  "Rd",  "",       "Round black body, coloured cap, tick ring"},
    {KnobStyle::Ptr, "Ptr", "",       "Flat-top silver pointer knob (tapered), dark pointer line"},
    {KnobStyle::Dm,  "Dm",  "",       "Chunky glossy domed cap in the item colour, pointer dot"},
    {KnobStyle::Sq,  "Sq",  "",       "Square rounded keycap with a dished top and legend line"},
    {KnobStyle::Seg, "Seg", "",       "Digital: dark cylinder inside a lit segmented arc (last segments red)"},
    {KnobStyle::Vfd, "Vfd", "",       "Digital: phosphor-glass disc with a vector fan of lit lines"},
    {KnobStyle::Cyb, "Cyb", "",       "Digital: chamfered dark frame, glowing outline, notched ring"},
    {KnobStyle::Fat, "Fat", "",       "Large black skirt with a thick white pointer running to the edge"},
    {KnobStyle::Pag, "Pag", "",       "Black ribbed octagonal bakelite with a glossy centre dome"},
    {KnobStyle::Lvr, "Lvr", "",       "Black bar lever selector on a round hub"},
    {KnobStyle::Tab, "Tab", "",       "Small black body with a short tab pointer"},
    {KnobStyle::Wh,  "Wh",  "",       "Black knurled skirt with a white cap and black line"},
    {KnobStyle::Knl, "Knl", "",       "Dark knurled body with a brushed aluminium cap and pointer"},
    {KnobStyle::Skt, "Skt", "",       "Black skirt with a bright pointer line running down its side"},
    {KnobStyle::Rib, "Rib", "",       "Large ribbed knob moulded in the item colour"},
    {KnobStyle::Chr, "Chr", "",       "Chrome skirt, glossy dark cap, white pointer, dotted ring"},
    {KnobStyle::Arc, "Arc", "",       "Digital: continuous lit arc band (amber to red) around a dark cap"},
    {KnobStyle::Brg, "Brg", "",       "Digital: rising bar-graph arc, bars grow with value"},
    {KnobStyle::Neo, "Neo", "",       "Digital: thin neon ring with glowing value arc and dot"},
    {KnobStyle::Hex, "Hex", "",       "Digital: hexagon frame with tinted glass and pointer"},
    {KnobStyle::Yel, "Yel", "",       "Digital: rugged rounded chassis frame in the item colour, black screen, cyan pointer"},
    {KnobStyle::Key, "Key", "",       "Round dished keycap with a coloured legend"},
    {KnobStyle::Brz, "Brz", "",       "Polished brass knurled knob, black pointer"},
    {KnobStyle::Chk, "Chk", "",       "Black chicken-head pointer knob with a white line"},
    {KnobStyle::Ivr, "Ivr", "",       "Cream bakelite fluted knob, dark pointer"},
    {KnobStyle::Mpl, "Mpl", "",       "Light maple wooden knob with inlaid pointer"},
    {KnobStyle::Led, "Led", "",       "Digital: dark cylinder inside a ring of lit LED dots"},
    {KnobStyle::Dbl, "Dbl", "",       "Stacked two-tier knob: silver outer ring, black inner knob"},
    {KnobStyle::Mini,"Mini","",       "Small trimmer knob with a white slot and tick ring"},
    {KnobStyle::Tl,  "Tl",  "",       "Tall black knurled knob with a white line"},
    {KnobStyle::Glw, "Glw", "",       "Neon: dark glass disc, glowing value ring and glowing pointer"},
    {KnobStyle::Grd, "Grd", "",       "Neon: perspective-grid face with a glowing pointer and dotted ring"},
    {KnobStyle::Dsh, "Dsh", "",       "Dash: ramp of bars filling to the value with a needle"},
    {KnobStyle::Mic, "Mic", "",       "Translucent crown pointer knob in the item colour with a white indicator, dotted ring"},
    {KnobStyle::Ebn, "Ebn", "",       "Ebony wood knob with a brass cap and an inlaid pointer line"},
    {KnobStyle::Wnt, "Wnt", "",       "Walnut wood top inside a chrome collar, pointer line"},
};
// Accepts a style code or a legacy name from older design files.
inline const StyleInfo* findStyle(const std::string& name) {
    for (const auto& s : knobStyles)
        if (name == s.code || (*s.legacy && name == s.legacy)) return &s;
    return nullptr;
}
// Whole-UI faceplate looks (colours are 0xAARRGGBB, JUCE-free).
struct Faceplate {
    const char* code; const char* name;
    std::uint32_t bgTop, bgBottom, panelTop, panelBottom, text, textMid, accent, bgFill, border, titleBar, rail, footer;
    int finish;   // plate surface: 0 flat, 1 brushed, 2 wood grain, 3 scanlines, 4 hazard stripes, 5 grid, 6 neon edge glow, 7 synth horizon
};
inline constexpr Faceplate faceplates[] = {
    {"Graphite","Graphite", 0xff242c31, 0xff151c20, 0xff394247, 0xff2a3034, 0xffdde1d5, 0xff909c9f, 0xffc7a66a, 0xff10171b, 0xff485156, 0xff121b20, 0xff0c1114, 0xff526065, 0},
    {"Black","Matte black", 0xff1a1a1c, 0xff0b0b0c, 0xff2a2a2d, 0xff18181a, 0xffe6e6e6, 0xff8b8b8e, 0xffd0d0d0, 0xff070708, 0xff505052, 0xff09090a, 0xff060606, 0xff5e5e60, 0},
    {"Navy","Navy blue", 0xff1c2d5a, 0xff0e1a3c, 0xff2a418a, 0xff1d2f66, 0xffe8eefc, 0xff9fb2e0, 0xff5bb8f0, 0xff09122a, 0xff50639f, 0xff0b1633, 0xff070e21, 0xff6c7ca6, 0},
    {"Cream","Cream", 0xffeee6cc, 0xffd9cfae, 0xfff6efd9, 0xffe4dbbd, 0xff23323a, 0xff5d7078, 0xff2f98c9, 0xff979079, 0xfff7f1df, 0xffb8af93, 0xff77715f, 0xff88918a, 0},
    {"Silver","Brushed silver", 0xffc9cdd0, 0xffa4a9ad, 0xffdfe2e4, 0xffc3c8cb, 0xff1b1f21, 0xff4c5458, 0xffc0392b, 0xff727679, 0xffe4e7e8, 0xff8b8f93, 0xff5a5c5f, 0xff6a7175, 1},
    {"Champagne","Champagne gold", 0xffd9c68a, 0xffb89f5a, 0xffe6d9a8, 0xffcdb878, 0xff201a0a, 0xff574a22, 0xff7a1f1f, 0xff806f3e, 0xffeadfb7, 0xff9c874c, 0xff655731, 0xff786735, 1},
    {"Teal","Vintage teal", 0xff2f6b78, 0xff1b4551, 0xff3f8795, 0xff2c6370, 0xffeaf6f8, 0xffa6ccd3, 0xffe8c15a, 0xff123038, 0xff619ca8, 0xff163a44, 0xff0e252c, 0xff759ca5, 0},
    {"Dash","Green phosphor dash", 0xff04100a, 0xff010502, 0xff0b1d12, 0xff06120a, 0xff7dff9a, 0xff3b9c55, 0xff39ff7a, 0xff000301, 0xff36453c, 0xff000401, 0xff000201, 0xff266737, 3},
    {"Amber","Amber LCD", 0xff150d02, 0xff080400, 0xff251603, 0xff170d02, 0xffffc34d, 0xffa8741a, 0xffff9a1f, 0xff050200, 0xff4c3f30, 0xff060300, 0xff040200, 0xff704c10, 3},
    {"Cyber","Cyber cyan", 0xff06141a, 0xff020609, 0xff0b242d, 0xff07161c, 0xff6cf2f8, 0xff2f8f98, 0xfff7e03c, 0xff010406, 0xff364b52, 0xff010507, 0xff010304, 0xff1f5f65, 5},
    {"Marine","Dark blue plate", 0xff111b30, 0xff0a1020, 0xff26385e, 0xff1a2848, 0xffe9edf5, 0xff9db0d0, 0xff6fa5d6, 0xff070b16, 0xff4d5b7a, 0xff080d1b, 0xff050811, 0xff697892, 0},
    {"Granite","Console grey plate", 0xff7d8185, 0xff63676b, 0xffb4b8bb, 0xff9a9ea2, 0xff14181a, 0xff3c4246, 0xffb03a2e, 0xff45484a, 0xffc1c4c7, 0xff54575a, 0xff36383a, 0xff494e52, 0},
    {"Royal","Royal blue plate", 0xff1a2670, 0xff0f1850, 0xff3347b5, 0xff25339a, 0xfff1f3ff, 0xffb0b9ee, 0xfff2d21e, 0xff0a1038, 0xff5768c2, 0xff0c1444, 0xff080d2c, 0xff7780b6, 0},
    {"Slate","Rack slate blue-grey", 0xff3c4a58, 0xff28333e, 0xff586a7a, 0xff435261, 0xffe6ebef, 0xffa5b4c2, 0xff7fc4e8, 0xff1c232b, 0xff768491, 0xff222b34, 0xff161c22, 0xff798693, 0},
    {"Periwinkle","Studio periwinkle", 0xff3a3f8a, 0xff262b6a, 0xff5a60bd, 0xff464ba4, 0xfff3f4ff, 0xffc2c6f2, 0xffffe27a, 0xff1a1e4a, 0xff777cc8, 0xff20245a, 0xff14173a, 0xff8b8fc2, 0},
    {"Leveler","Pale blue-grey leveler", 0xffa9b3b8, 0xff8a959b, 0xffc4ccd0, 0xffa8b2b8, 0xff1c2428, 0xff4a565c, 0xff9c2f2f, 0xff60686c, 0xffced5d8, 0xff757e83, 0xff4b5155, 0xff606c72, 1},
    {"Ultramarine","Anodised ultramarine", 0xff16246e, 0xff0c1648, 0xff2b43b0, 0xff1d3190, 0xffeef1ff, 0xffaebaf0, 0xff41c3ff, 0xff080f32, 0xff5164be, 0xff0a123d, 0xff060c27, 0xff7580b5, 0},
    {"Coal","Module coal black", 0xff1f2022, 0xff0d0d0e, 0xff2d2f31, 0xff1a1b1c, 0xffeeeeee, 0xff9aa8ae, 0xff4aa8d8, 0xff090909, 0xff525456, 0xff0b0b0b, 0xff070707, 0xff687176, 1},
    {"Steel","Brushed steel", 0xff8c9296, 0xff70777b, 0xffa7adb1, 0xff8d9498, 0xff101416, 0xff3a4246, 0xffb3342a, 0xff4e5356, 0xffb6bbbf, 0xff5f6568, 0xff3d4143, 0xff4c5458, 1},
    {"Scarlet","Signal scarlet module", 0xff6e1f14, 0xff45120b, 0xffb8321f, 0xff8c2415, 0xfff6e6c4, 0xffe0b89a, 0xffffd34a, 0xff300c07, 0xffc45647, 0xff3a0f09, 0xff250906, 0xffa97d67, 0},
    {"Cobalt","Rack cobalt", 0xff1a2f66, 0xff10204a, 0xff2f58b8, 0xff224592, 0xfff0f4ff, 0xffaabdea, 0xffffffff, 0xff0b1633, 0xff5476c4, 0xff0d1b3e, 0xff081128, 0xff7486b2, 0},
    {"Stealth","Stealth black", 0xff151618, 0xff080809, 0xff232527, 0xff151617, 0xfff4f4f4, 0xff8a8d90, 0xff3a8bff, 0xff050506, 0xff4a4c4d, 0xff060607, 0xff040404, 0xff5c5e60, 1},
    {"Walnut","Walnut cheek wood", 0xff4a3322, 0xff2e1f14, 0xff75503a, 0xff573a28, 0xfff3e3c4, 0xffcfae86, 0xffe8b15a, 0xff20150e, 0xff8d6f5d, 0xff271a11, 0xff19110b, 0xff967b5e, 2},
    {"Midnight","Midnight blue", 0xff101a3c, 0xff080e24, 0xff1c2c62, 0xff131f48, 0xffe3eaff, 0xff8fa2d4, 0xffd8d8d8, 0xff050919, 0xff44517e, 0xff060b1e, 0xff040713, 0xff5f6e96, 0},
    {"Azure","Azure plate", 0xff1b5a94, 0xff103e6c, 0xff2d7ac2, 0xff2262a0, 0xfff2f8ff, 0xffb4d4f0, 0xffffffff, 0xff0b2b4b, 0xff5291cc, 0xff0d345b, 0xff08223b, 0xff7a9fc1, 0},
    {"Oxblood","Oxblood leather", 0xff4a1a1a, 0xff2e0e0e, 0xff74302f, 0xff582222, 0xfff1dcc8, 0xffc79f88, 0xffe3b07a, 0xff200909, 0xff8d5554, 0xff270b0b, 0xff190707, 0xff916c5d, 0},
    {"Cognac","Cognac tan", 0xff9a6a3a, 0xff744a24, 0xffbd8a55, 0xffa07038, 0xff23150a, 0xff5b3d1d, 0xff2a4a7a, 0xff513319, 0xffc89f73, 0xff623e1e, 0xff3f2813, 0xff63411f, 0},
    {"Canary","Cyberpunk yellow chassis", 0xffc7a812, 0xff9a8208, 0xffe8c820, 0xffcaa913, 0xff14110a, 0xff4a420e, 0xff16b4c4, 0xff6b5b05, 0xffecd148, 0xff826e06, 0xff544704, 0xff66580b, 4},
    {"Mesh","Cyberpunk mesh teal", 0xff041b1f, 0xff020b0d, 0xff0a3a40, 0xff062a2f, 0xff7ff5e8, 0xff32a89c, 0xffff4d6d, 0xff010709, 0xff365d62, 0xff01090b, 0xff010607, 0xff217169, 5},
    {"Redline","Cyberpunk combat red", 0xff1a0508, 0xff0c0204, 0xff2c0a10, 0xff1c060a, 0xffff3b4a, 0xffa6222e, 0xffff3b4a, 0xff080102, 0xff51363b, 0xff0a0103, 0xff060102, 0xff70161f, 3},
    {"Monolith","Cyberpunk corp black-red", 0xff161618, 0xff08080a, 0xff242427, 0xff151517, 0xfff2f2f2, 0xffb02a2a, 0xffe5202e, 0xff050507, 0xff4b4b4d, 0xff060608, 0xff040405, 0xff751e1e, 1},
    {"Olive","Cyberpunk olive tactical", 0xff2f362a, 0xff1c2018, 0xff474f3f, 0xff363d30, 0xffe0e4cc, 0xffa5ae8c, 0xffd9b43a, 0xff131610, 0xff686e61, 0xff171b14, 0xff0f110d, 0xff757c63, 0},
    {"Synthwave","Cyberpunk synthwave neon", 0xff24083a, 0xff12041f, 0xff3b1260, 0xff2a0c47, 0xffff7ad9, 0xffa66bd8, 0xff35f2ff, 0xff0c0215, 0xff5e3c7c, 0xff0f031a, 0xff090211, 0xff724697, 5},
    {"Optic","Cyberpunk optic blue", 0xff061830, 0xff030b18, 0xff0e2c52, 0xff091f3c, 0xff9fe8ff, 0xff4a8fb8, 0xffff5ec4, 0xff020710, 0xff395171, 0xff020914, 0xff01060d, 0xff316080, 5},
    {"Ochre","Retro computer ochre", 0xffb59a2a, 0xff8c761a, 0xffd2b83c, 0xffb59a2a, 0xff1a1608, 0xff4d4313, 0xff1c1c1c, 0xff625212, 0xffdac45f, 0xff776416, 0xff4d400e, 0xff635415, 0},
    {"Rust","Industrial rust", 0xff4a3c33, 0xff2c231d, 0xff6b5648, 0xff4f3e33, 0xfff2dcc8, 0xffc8a98f, 0xffff7a1a, 0xff1e1814, 0xff857468, 0xff251d18, 0xff18130f, 0xff917a67, 0},
    {"Caution","Caution black-yellow", 0xff1a1a1a, 0xff0a0a0a, 0xff2a2a2a, 0xff181818, 0xffffd800, 0xffb39600, 0xffffd800, 0xff070707, 0xff505050, 0xff080808, 0xff050505, 0xff776503, 4},
    {"Lime","Lime LCD dash", 0xff1c2108, 0xff0e1104, 0xff2d340c, 0xff1f2408, 0xffd7ff3a, 0xff88a31f, 0xffd7ff3a, 0xff090b02, 0xff525837, 0xff0b0e03, 0xff070902, 0xff5d6f15, 3},
    {"Neon","Neon glow edge", 0xff05080f, 0xff020307, 0xff0b1220, 0xff060b16, 0xff7ff9ff, 0xff3a8f9a, 0xffff4fd8, 0xff010204, 0xff363c48, 0xff010205, 0xff010103, 0xff265e66, 6},
    {"Horizon","Synth horizon", 0xff1a0733, 0xff0b031a, 0xff2e0f55, 0xff1e0a3a, 0xffffb0f0, 0xffa56ad0, 0xff35f2ff, 0xff070212, 0xff533a73, 0xff090216, 0xff06010e, 0xff6f4590, 7},
};
inline const Faceplate* findFaceplate(const std::string& code) {
    for (const auto& f : faceplates) if (code == f.code) return &f;
    return nullptr;
}
struct Item {
    std::string id, parameter, label, style = "metal";
    std::string colour = "#CE9435";
    float x=0, y=0, width=70, height=76, fontSize=10;
    // Printed marks (scale ticks and dots around a knob): "auto" picks dark marks on a light plate and light
    // marks on a dark plate, "light" and "dark" force one. Stored as an optional 11th CSV column.
    std::string marks = "auto";
    bool operator==(const Item&) const = default;
};
inline bool validMarks(const std::string& m) { return m=="auto" || m=="light" || m=="dark"; }
inline void validate(const Item& i) {
    if (i.id.empty() || i.id.find_first_of("\r\n") != std::string::npos)
        throw std::runtime_error("Control needs a valid ID");
    if (!findStyle(i.style))
        throw std::runtime_error("Unknown knob style: " + i.style);
    if (i.colour.size()!=7 || i.colour[0]!='#' ||
        i.colour.find_first_not_of("0123456789abcdefABCDEF",1)!=std::string::npos)
        throw std::runtime_error("Colour must be #RRGGBB");
    if (!validMarks(i.marks)) throw std::runtime_error("Marks must be auto, light or dark");
    for (float v : {i.x,i.y,i.width,i.height,i.fontSize})
        if (!std::isfinite(v)) throw std::runtime_error("Geometry must be finite");
    if (i.width<20 || i.height<20 || i.width>4096 || i.height>4096 ||
        i.fontSize<6 || i.fontSize>72 || std::abs(i.x)>8192 || std::abs(i.y)>8192)
        throw std::runtime_error("Geometry is outside supported limits");
}
inline std::string quote(const std::string& s) {
    std::string out="\"";
    for (char c:s) { if(c=='\r'||c=='\n') throw std::runtime_error("Use single-line text");
        if(c=='\"') out+='\"'; out+=c; }
    return out+'\"';
}
// Quotes a field only when it has to be (a comma, quote or edge space), so files stay readable and a one-value edit
// is a one-line diff.
inline std::string quoteIfNeeded(const std::string& s) {
    const bool plain = s.find_first_of(",\"") == std::string::npos && (s.empty() || (s.front() != ' ' && s.back() != ' '));
    return plain && s.find_first_of("\r\n") == std::string::npos ? s : quote(s);
}
inline std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> out; std::string value; bool quoted=false, closed=false;
    for (std::size_t n=0;n<line.size();++n) {
        char c=line[n];
        if(quoted) { if(c=='\"') { if(n+1<line.size() && line[n+1]=='\"') {value+='\"';++n;}
            else {quoted=false;closed=true;} } else value+=c; }
        else if(c==',') {out.push_back(value);value.clear();closed=false;}
        else if(c=='\"' && value.empty() && !closed) quoted=true;
        else {if(closed || c=='\"') throw std::runtime_error("Malformed CSV quoting");value+=c;}
    }
    if(quoted) throw std::runtime_error("Unclosed CSV quote");
    out.push_back(value); return out;
}
inline constexpr auto headerV1="id,parameter,label,style,colour,x,y,width,height,font size";   // files saved before `marks` existed
inline constexpr auto header="id,parameter,label,style,colour,x,y,width,height,font size,marks";
// The `marks` column is only written when some item uses it, so a design that never touches marks keeps the original
// ten-column format byte for byte.
inline void writeDesign(std::ostream& out, const std::vector<Item>& items) {
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    bool anyMarks = false; for (const auto& i : items) if (i.marks != "auto") anyMarks = true;
    out << "GoodLookinUI design version 1\n" << (anyMarks ? header : headerV1) << '\n';
    std::set<std::string> ids;
    for(const auto& i:items) {validate(i); if(!ids.insert(i.id).second) throw std::runtime_error("Duplicate ID");
        out<<quoteIfNeeded(i.id)<<','<<quoteIfNeeded(i.parameter)<<','<<quoteIfNeeded(i.label)<<','<<quoteIfNeeded(i.style)<<','<<quoteIfNeeded(i.colour)
           <<','<<i.x<<','<<i.y<<','<<i.width<<','<<i.height<<','<<i.fontSize;
        if (anyMarks) out<<','<<quoteIfNeeded(i.marks);
        out<<'\n';}
    if(!out) throw std::runtime_error("Could not write design");
}
inline std::vector<Item> readDesign(std::istream& in) {
    std::string line; auto next=[&]{ std::getline(in,line); if(!line.empty()&&line.back()=='\r') line.pop_back(); };
    next(); if(line!="GoodLookinUI design version 1" && line!="HardwareUI design version 1") throw std::runtime_error("Unsupported design version");
    next(); const bool hasMarks=(line==header); if(!hasMarks && line!=headerV1) throw std::runtime_error("Invalid design columns");
    std::vector<Item> result; std::set<std::string> ids;
    while(std::getline(in,line)) {if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        auto f=fields(line);if(f.size()!=(hasMarks?11u:10u))throw std::runtime_error(hasMarks?"Expected eleven columns":"Expected ten columns");
        auto number=[&](int n){std::size_t end=0;float v=std::stof(f[n],&end);
            if(end!=f[n].size())throw std::runtime_error("Invalid number");return v;};
        Item i{f[0],f[1],f[2],f[3],f[4],number(5),number(6),number(7),number(8),number(9),hasMarks?f[10]:std::string("auto")};
        validate(i);if(!ids.insert(i.id).second)throw std::runtime_error("Duplicate ID");result.push_back(i);
    }
    if(in.bad())throw std::runtime_error("Could not read design");return result;
}
// Critically damped motion; display animation never delays DSP parameter updates.
struct Motion {
    double position=0, velocity=0;
    void step(double target,double seconds,double frequency=18) {
        seconds=std::clamp(seconds,0.0,0.1);
        const double offset=position-target, decay=std::exp(-frequency*seconds);
        const double term=velocity+frequency*offset;
        position=target+(offset+term*seconds)*decay;
        velocity=(velocity-frequency*term*seconds)*decay;
    }
};
}
