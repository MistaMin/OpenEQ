// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Marcos Deida
#pragma once
#include <goodlookinui/Design.h>
#include "Knobs.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <fstream>
#include <functional>

namespace goodlookinui::juce_adapter {
inline juce::Colour colour(const std::string& hex) {
    return juce::Colour::fromString("ff" + juce::String(hex.substr(1)));
}
// Console hardware is generated at the component's current scale; no bitmaps.
inline void drawScrew(juce::Graphics& g, float x, float y) {
    auto r=juce::Rectangle<float>(8,8).withCentre({x,y});
    g.setColour(juce::Colour(0xff0d1113));g.fillEllipse(r.expanded(1));
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff7c8589),x-3,y-3,juce::Colour(0xff252b2e),x+3,y+3,false));g.fillEllipse(r);
    g.setColour(juce::Colour(0xff121719));g.drawLine(x-2,y+1,x+2,y-1,1.3f);
}
// Surface detail for plate finishes (see Faceplate::finish). Uses translucent
// black/white so it works over any plate colour.
inline void drawFinish(juce::Graphics& g, juce::Rectangle<float> r, int finish) {
    if(finish<=0) return;
    g.saveState();
    juce::Path clip;clip.addRoundedRectangle(r,2.0f);g.reduceClipRegion(clip,{});
    const float x0=r.getX(),x1=r.getRight(),y0=r.getY(),y1=r.getBottom();
    switch(finish) {
    case 1: // brushed: fine horizontal streaks
        for(int y=int(y0);y<int(y1);++y){const unsigned h=unsigned(y)*2654435761u;
            g.setColour(juce::Colour((h>>7)&1?0x0dffffff:0x0d000000));
            g.drawHorizontalLine(y,x0+float(h%9),x1-float((h>>5)%13));}
        break;
    case 2: // wood grain: wavy lines
        g.setColour(juce::Colour(0x26000000));
        for(float y=y0+3;y<y1;y+=5.0f){juce::Path p;
            for(float x=x0;x<=x1;x+=6.0f){const float yy=y+std::sin(x*0.045f+y*0.31f)*2.2f;x==x0?p.startNewSubPath(x,yy):p.lineTo(x,yy);}
            g.strokePath(p,juce::PathStrokeType(0.8f));}
        break;
    case 3: // scanlines with a faint glow at the top
        g.setColour(juce::Colour(0x1c000000));
        for(float y=y0;y<y1;y+=3.0f) g.drawHorizontalLine(int(y),x0,x1);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0x14ffffff),0,y0,juce::Colour(0x00ffffff),0,y0+60,false));
        g.fillRect(r);
        break;
    case 4: // hazard stripes along the top and bottom edges
        for(float yBand:{y0+1.0f,y1-7.0f})
            for(float x=x0-8;x<x1;x+=12.0f){juce::Path p;
                p.addQuadrilateral(x,yBand+6,x+6,yBand+6,x+12,yBand,x+6,yBand);
                g.setColour(juce::Colour(0xffe8c820));g.fillPath(p);}
        g.setColour(juce::Colour(0x55000000));
        g.fillRect(x0,y0+1,x1-x0,6.0f);
        break;
    case 6: // neon edge glow: bright inner border with a soft halo and corner ticks
        for(int k=5;k>=1;--k){g.setColour(juce::Colour(0xff39f2ff).withAlpha(0.035f*float(6-k)));g.drawRoundedRectangle(r.reduced(1.0f+float(k)*0.7f),2,1.6f);}
        g.setColour(juce::Colour(0xcc39f2ff));g.drawRoundedRectangle(r.reduced(2.0f),2,1.0f);
        g.setColour(juce::Colour(0xffff4fd8));g.drawLine(x0+2,y0+2,x0+22,y0+2,2.0f);g.drawLine(x1-22,y1-2,x1-2,y1-2,2.0f);
        break;
    case 7: { // synth horizon: sun gradient and a perspective grid in the lower half
        const float hy=y0+(y1-y0)*0.58f;
        g.setGradientFill(juce::ColourGradient(juce::Colour(0x00ff4fd8),0,y0,juce::Colour(0x55ff4fd8),0,hy,false));g.fillRect(x0,y0,x1-x0,hy-y0);
        g.setColour(juce::Colour(0x8835f2ff));g.drawHorizontalLine(int(hy),x0,x1);
        for(int i=-8;i<=8;++i)g.drawLine(x0+(x1-x0)*0.5f+float(i)*3.0f,hy,x0+(x1-x0)*0.5f+float(i)*(x1-x0)*0.16f,y1,0.7f);
        for(int j=1;j<=6;++j){const float y=hy+std::pow(float(j)/6.0f,2.0f)*(y1-hy);g.drawHorizontalLine(int(y),x0,x1);}
        break; }
    default: // 5: faint grid with corner brackets
        g.setColour(juce::Colour(0x12ffffff));
        for(float x=x0+14;x<x1;x+=14.0f) g.drawVerticalLine(int(x),y0,y1);
        for(float y=y0+14;y<y1;y+=14.0f) g.drawHorizontalLine(int(y),x0,x1);
        g.setColour(juce::Colour(0x66ffffff));
        for(auto c:{juce::Point<float>(x0,y0),juce::Point<float>(x1,y0),juce::Point<float>(x0,y1),juce::Point<float>(x1,y1)}){
            const float sx=c.x==x0?1.0f:-1.0f,sy=c.y==y0?1.0f:-1.0f;
            g.drawLine(c.x,c.y+sy*10,c.x,c.y,1.2f);g.drawLine(c.x,c.y,c.x+sx*10,c.y,1.2f);}
        break;
    }
    g.restoreState();
}
inline void drawPanel(juce::Graphics& g, juce::Rectangle<float> r,
                      juce::Colour top=juce::Colour(0xff394247), juce::Colour bottom=juce::Colour(0xff2a3034), int finish=0) {
    g.setColour(juce::Colour(0x88000000));g.fillRoundedRectangle(r.translated(0,2),3);
    g.setGradientFill(juce::ColourGradient(top,r.getX(),r.getY(),
        bottom,r.getRight(),r.getBottom(),false));g.fillRoundedRectangle(r,2);
    drawFinish(g,r,finish);
    g.setColour(juce::Colour(0xff101618));g.drawRoundedRectangle(r,2,1);
    g.setColour(juce::Colour(0x22ffffff));g.drawHorizontalLine(int(r.getY()+1),r.getX()+2,r.getRight()-2);
}
inline void drawKey(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, bool down) {
    g.setColour(juce::Colour(0xff131719));g.fillRoundedRectangle(r.expanded(2),3);
    auto face=r.translated(0,down?1.5f:0);
    g.setColour(juce::Colour(0xff080c0d));g.fillRoundedRectangle(face.translated(0,2),2);
    g.setGradientFill(juce::ColourGradient(base.brighter(0.18f),face.getX(),face.getY(),
        base.darker(down?0.30f:0.16f),face.getRight(),face.getBottom(),false));g.fillRoundedRectangle(face,2);
    g.setColour(juce::Colour(0x66ffffff));g.drawHorizontalLine(int(face.getY()+1),face.getX()+2,face.getRight()-2);
    g.setColour(juce::Colour(0x44000000));g.drawRoundedRectangle(face,2,0.8f);
}
// True when marks should be dark. `backdrop` is the plate colour behind the knob (transparent = unknown, assume dark).
inline bool useDarkMarks(const Item& item, juce::Colour backdrop) {
    if(item.marks=="dark") return true;
    if(item.marks=="light") return false;
    return backdrop.getAlpha()>0 && backdrop.getPerceivedBrightness()>0.55f;
}
inline void drawConsoleKnob(juce::Graphics& g,juce::Rectangle<float> bounds,double proportion,const Item& item,bool active,bool darkMarks=false) {
    const float d=juce::jmin(bounds.getWidth(),bounds.getHeight());
    auto outer=juce::Rectangle<float>(d,d).withCentre(bounds.getCentre());
    auto c=outer.getCentre();const float r=d*0.5f;
    // Printed calibration marks remain fixed while the grip and pointer rotate.
    g.setColour(darkMarks?juce::Colour(active?0xff2a2f32:0xff7d8788):juce::Colour(active?0xffc5cabf:0xff707a7b));
    for(int n=0;n<13;++n){float a=(-0.8f+1.6f*float(n)/12)*juce::MathConstants<float>::pi;
        juce::Point<float> v(std::sin(a),-std::cos(a));
        g.drawLine({c+v*r*0.88f,c+v*r*(n%3==0?1.0f:0.95f)},n%3==0?1.2f:0.7f);}
    auto body=outer.reduced(d*0.13f);
    for(int n=5;n>0;--n){g.setColour(juce::Colour(0x09000000));g.fillEllipse(body.expanded(float(n)*0.7f).translated(1,3));}
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff565c60),c.x-r,c.y-r,
        juce::Colour(0xff080a0c),c.x+r,c.y+r,false));g.fillEllipse(body);
    const float a=float((proportion*1.6-0.8)*juce::MathConstants<double>::pi);
    const float gripR=body.getWidth()*0.5f;
    for(int n=0;n<28;++n){float t=a+float(n)*juce::MathConstants<float>::twoPi/28;
        juce::Point<float> v(std::sin(t),-std::cos(t));
        auto q=c+v*gripR*0.92f;
        const float light=juce::jlimit(0.0f,1.0f,0.5f-0.35f*(v.x+v.y));
        g.setColour(juce::Colour(0xff080a0b).interpolatedWith(juce::Colour(0xff61686b),light));
        g.drawLine({c+v*gripR*0.73f,q},2.2f);}
    auto cap=body.reduced(d*0.055f);
    auto base=active?colour(item.colour):juce::Colour(0xff697170);
    g.setGradientFill(juce::ColourGradient(base.brighter(0.28f),cap.getX(),cap.getY(),
        base.darker(0.42f),cap.getRight(),cap.getBottom(),false));g.fillEllipse(cap);
    g.setColour(base.darker(0.6f));g.drawEllipse(cap,0.8f);
    auto face=cap.reduced(d*0.032f);
    g.setGradientFill(juce::ColourGradient(base.brighter(0.08f),face.getX(),face.getY(),
        base.darker(0.10f),face.getRight(),face.getBottom(),false));g.fillEllipse(face);
    g.setColour(juce::Colour(0x27ffffff));g.drawEllipse(face,0.7f);
    // Recessed white pointer cut into the coloured moulded cap.
    juce::Point<float> v(std::sin(a),-std::cos(a));
    auto tip=c+v*face.getWidth()*0.43f;auto tail=c+v*face.getWidth()*0.12f;
    g.setColour(juce::Colour(0x77000000));g.drawLine({tail.translated(0.7f,0.7f),tip.translated(0.7f,0.7f)},3.4f);
    g.setColour(juce::Colour(0xfff5f2df));g.drawLine({tail,tip},2.5f);
}
inline void drawKnob(juce::Graphics& g, juce::Rectangle<float> bounds,
                     double proportion, const Item& item, bool active, juce::Colour backdrop = juce::Colour()) {
    const auto* info=findStyle(item.style);
    const auto style=info?info->style:KnobStyle::Brit;
    const bool dark=useDarkMarks(item,backdrop);
    if(style==KnobStyle::Brit) {drawConsoleKnob(g,bounds,proportion,item,active,dark);return;}
    const float d=juce::jmin(bounds.getWidth(),bounds.getHeight());
    const auto c=bounds.getCentre();
    if(!active) g.beginTransparencyLayer(0.55f);
    knobs::Ctx k{g,c,d*0.5f,float((proportion*1.6-0.8)*juce::MathConstants<double>::pi),
                 active?colour(item.colour):juce::Colour(0xff697170), dark};
    knobs::draw(style,k);
    if(!active) g.endTransparencyLayer();
}

}
