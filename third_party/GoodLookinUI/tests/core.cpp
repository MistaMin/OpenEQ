// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#include <goodlookinui/Design.h>
#include <cassert>
#include <cctype>
#include <set>
#include <iostream>
int main(){
 using namespace goodlookinui;
 Item i{"gain","preampGain","Gain, \"input\"","console","#AAbb01",12,15,70,76,11};
 std::stringstream file;writeDesign(file,{i});assert(readDesign(file)==std::vector<Item>{i});
 i.style="console";std::stringstream console;writeDesign(console,{i});assert(readDesign(console)==std::vector<Item>{i});
 auto legacyText=file.str();legacyText.replace(0,std::string("GoodLookinUI").size(),"HardwareUI");std::stringstream legacy(legacyText);assert(readDesign(legacy)==std::vector<Item>{i});
 auto bad=[&](std::string text){std::stringstream s(text);try{readDesign(s);}catch(...){return;}throw std::runtime_error("Accepted bad file");};
 bad(std::string("GoodLookinUI design version 1\n")+header+"\n\"unclosed,a,a,metal,#ffffff,0,0,70,76,10\n");
 bad("GoodLookinUI design version 2\n");
 std::stringstream duplicate;duplicate<<"GoodLookinUI design version 1\n"<<header<<"\na,a,a,metal,#ffffff,0,0,70,76,10\na,a,a,metal,#ffffff,0,0,70,76,10\n";bad(duplicate.str());
 for(auto value:{"nan","inf","12junk","-1"})bad(std::string("GoodLookinUI design version 1\n")+header+"\na,a,a,metal,#ffffff,0,0,"+value+",76,10\n");
 for(const auto& s:knobStyles){Item j=i;j.style=s.code;std::stringstream f;writeDesign(f,{j});assert(readDesign(f)==std::vector<Item>{j});}
 assert(findStyle("metal")->style==KnobStyle::Mtl && findStyle("bakelite")->style==KnobStyle::Bk && findStyle("ivory")->style==KnobStyle::Slv && findStyle("console")->style==KnobStyle::Brit);
 assert(!findStyle("bogus"));
 assert(findFaceplate("Navy") && findFaceplate("Graphite") && !findFaceplate("Nope") && std::size(faceplates)==40);
 {std::set<std::string> codes;
  const std::set<std::string> approvedLabels = {
    "A",
    "Amber",
    "Amber LCD",
    "Anodised ultramarine",
    "Arc",
    "Azure",
    "Azure plate",
    "Bk",
    "Black",
    "Black bakelite teardrop pointer over a ribbed base",
    "Black bar lever selector on a round hub",
    "Black chicken-head pointer knob with a white line",
    "Black fluted grip, large coloured flat cap, recessed white pointer",
    "Black knurled skirt with a white cap and black line",
    "Black ribbed octagonal bakelite with a glossy centre dome",
    "Black skirt with a bright pointer line running down its side",
    "Brg",
    "Brit",
    "Brushed silver",
    "Brushed steel",
    "Brz",
    "Canary",
    "Caution",
    "Caution black-yellow",
    "Champagne",
    "Champagne gold",
    "Chk",
    "Chr",
    "Chrome collar, grey fluted grip, flat cap, dotted scale ring",
    "Chrome skirt, glossy dark cap, white pointer, dotted ring",
    "Chunky glossy domed cap in the item colour, pointer dot",
    "Coal",
    "Cobalt",
    "Cognac",
    "Cognac tan",
    "Console grey plate",
    "Cream",
    "Cream bakelite fluted knob, dark pointer",
    "Cyb",
    "Cyber",
    "Cyber cyan",
    "Cyberpunk combat red",
    "Cyberpunk corp black-red",
    "Cyberpunk mesh teal",
    "Cyberpunk olive tactical",
    "Cyberpunk optic blue",
    "Cyberpunk synthwave neon",
    "Cyberpunk yellow chassis",
    "Dark blue plate",
    "Dark knurled body with a brushed aluminium cap and pointer",
    "Dash",
    "Dash: ramp of bars filling to the value with a needle",
    "Dbl",
    "Digital: chamfered dark frame, glowing outline, notched ring",
    "Digital: continuous lit arc band (amber to red) around a dark cap",
    "Digital: dark cylinder inside a lit segmented arc (last segments red)",
    "Digital: dark cylinder inside a ring of lit LED dots",
    "Digital: hexagon frame with tinted glass and pointer",
    "Digital: phosphor-glass disc with a vector fan of lit lines",
    "Digital: rising bar-graph arc, bars grow with value",
    "Digital: rugged rounded chassis frame in the item colour, black screen, cyan pointer",
    "Digital: thin neon ring with glowing value arc and dot",
    "Dm",
    "Dsh",
    "Ebn",
    "Ebony wood knob with a brass cap and an inlaid pointer line",
    "FS",
    "Fat",
    "Flared silver crown with a large flat matte cap (any colour, dark for charcoal), dotted ring",
    "Flat-top silver pointer knob (tapered), dark pointer line",
    "Glw",
    "Granite",
    "Graphite",
    "Grd",
    "Green phosphor dash",
    "Hex",
    "Horizon",
    "Industrial rust",
    "Ivr",
    "Key",
    "Knl",
    "Knurled aluminium body, brushed top, dark pointer line",
    "Large black skirt with a thick white pointer running to the edge",
    "Large ribbed knob moulded in the item colour",
    "Led",
    "Leveler",
    "Light maple wooden knob with inlaid pointer",
    "Lime",
    "Lime LCD dash",
    "Lvr",
    "Marine",
    "Matte black",
    "Matte black cylinder, fixed tick ring, pointer line",
    "Mesh",
    "Mic",
    "Midnight",
    "Midnight blue",
    "Mini",
    "Module coal black",
    "Monolith",
    "Mpl",
    "Mtl",
    "N",
    "Navy",
    "Navy blue",
    "Neo",
    "Neon",
    "Neon glow edge",
    "Neon: dark glass disc, glowing value ring and glowing pointer",
    "Neon: perspective-grid face with a glowing pointer and dotted ring",
    "Ochre",
    "Olive",
    "Optic",
    "Oxblood",
    "Oxblood leather",
    "Pag",
    "Pale blue-grey leveler",
    "Pale-grey skirt, coloured cap, black pointer line",
    "Periwinkle",
    "Pie",
    "Polished brass knurled knob, black pointer",
    "Ptr",
    "Rack cobalt",
    "Rack slate blue-grey",
    "Rd",
    "Redline",
    "Retro computer ochre",
    "Rib",
    "Round black body, coloured cap, tick ring",
    "Round dished keycap with a coloured legend",
    "Royal",
    "Royal blue plate",
    "Rust",
    "Scarlet",
    "Seg",
    "Signal scarlet module",
    "Silver",
    "Silver-grey cylinder, dark skirt ring with dots, black pointer",
    "Skt",
    "Slate",
    "Slv",
    "Small black body with a short tab pointer",
    "Small trimmer knob with a white slot and tick ring",
    "Snk",
    "Sq",
    "Square rounded keycap with a dished top and legend line",
    "Stacked two-tier knob: silver outer ring, black inner knob",
    "Stealth",
    "Stealth black",
    "Steel",
    "Studio periwinkle",
    "Synth horizon",
    "Synthwave",
    "Tab",
    "Tall black knurled knob with a white line",
    "Teal",
    "Tl",
    "Translucent crown pointer knob in the item colour with a white indicator, dotted ring",
    "Turned wooden knob, grain, inlaid pointer line",
    "Ultramarine",
    "Vfd",
    "Vintage teal",
    "Walnut",
    "Walnut cheek wood",
    "Walnut wood top inside a chrome collar, pointer line",
    "Wd",
    "Wh",
    "White serrated pie-crust skirt, black cap, pointer line",
    "Wnt",
    "Yel",
  };
  for(const auto& f:faceplates){assert(codes.insert(f.code).second);assert(f.finish>=0&&f.finish<=7);assert(approvedLabels.count(f.code)&&approvedLabels.count(f.name));}
  for(const auto& k:knobStyles){assert(approvedLabels.count(k.code)&&approvedLabels.count(k.description));}}
 // printed-mark option: round trip, legacy 10-column files still load as "auto", bad values rejected
 {Item mk=i;for(auto m:{"auto","light","dark"}){mk.marks=m;std::stringstream f;writeDesign(f,{mk});assert(readDesign(f)==std::vector<Item>{mk});}
  std::stringstream legacyFile;legacyFile<<"GoodLookinUI design version 1\n"<<headerV1<<"\na,a,Gain,Brit,#ffffff,1,2,70,76,10\n";
  auto old=readDesign(legacyFile);assert(old.size()==1&&old[0].marks=="auto"&&old[0].style=="Brit");
  Item badMarks=i;badMarks.marks="blue";bool threw=false;try{validate(badMarks);}catch(...){threw=true;}assert(threw);
  std::stringstream wrongCols;wrongCols<<"GoodLookinUI design version 1\n"<<header<<"\na,a,Gain,Brit,#ffffff,1,2,70,76,10\n";threw=false;try{readDesign(wrongCols);}catch(...){threw=true;}assert(threw);}
 // minimal quoting + the marks column is only written when used
 {Item q=i;q.label="plain";std::stringstream a;writeDesign(a,{q});assert(a.str().find('"')==std::string::npos&&a.str().find("marks")==std::string::npos&&a.str().find(headerV1)!=std::string::npos);
  assert(readDesign(a)==std::vector<Item>{q});
  q.marks="dark";std::stringstream b;writeDesign(b,{q});assert(b.str().find("marks")!=std::string::npos&&readDesign(b)==std::vector<Item>{q});
  q.marks="auto";q.label="a, \"b\"";std::stringstream c;writeDesign(c,{q});assert(c.str().find("\"a, \"\"b\"\"\"")!=std::string::npos&&readDesign(c)==std::vector<Item>{q});
  // a file in the original format loads and is written back byte for byte
  const std::string original=std::string("GoodLookinUI design version 1\n")+headerV1+"\nlcFreq,lcFreq,FREQ,console,#CBCBBC,12,52,150,98,10\nlcQ,lcQ,Q,console,#CBCBBC,12,152,150,98,10\n";
  std::stringstream o(original);auto items=readDesign(o);std::stringstream back;writeDesign(back,items);assert(back.str()==original);}
 Motion m;for(int n=0;n<600;++n){m.step(1,1.0/60);assert(m.position>=0 && m.position<=1.00001);}assert(std::abs(m.position-1)<1e-8);
 std::cout<<"Design roundtrip, invalid input, duplicate IDs and motion checks passed\n";
}
