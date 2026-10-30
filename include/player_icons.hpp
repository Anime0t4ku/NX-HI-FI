#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
namespace nxh {
// Normalised vector strokes. Rasterised with coverage sampling once per output scale.
struct PlayerIconShape {
    struct Point{float x,y;};struct Stroke{Point a,b;float width;};
    std::vector<Stroke> strokes;std::vector<std::vector<Point>> fills;
    void line(float x,float y,float x2,float y2,float width=.075f){strokes.push_back({{x,y},{x2,y2},width});}
    void path(std::initializer_list<Point> points,float width=.075f){auto a=points.begin();for(auto b=a+1;b!=points.end();++a,++b)line(a->x,a->y,b->x,b->y,width);}
    void box(float x,float y,float w,float h,float width=.065f){path({{x,y},{x+w,y},{x+w,y+h},{x,y+h},{x,y}},width);}
    void circle(float x,float y,float radius,float width=.065f){for(int i=0;i<40;i++){float a=i*6.2831853f/40,b=(i+1)*6.2831853f/40;line(x+std::cos(a)*radius,y+std::sin(a)*radius,x+std::cos(b)*radius,y+std::sin(b)*radius,width);}}
    bool hit(float x,float y)const{
        for(auto &s:strokes){float dx=s.b.x-s.a.x,dy=s.b.y-s.a.y,len=dx*dx+dy*dy;float t=len>0?std::clamp(((x-s.a.x)*dx+(y-s.a.y)*dy)/len,0.f,1.f):0;float ex=x-s.a.x-dx*t,ey=y-s.a.y-dy*t;if(ex*ex+ey*ey<=s.width*s.width*.25f)return true;}
        for(auto &polygon:fills){bool inside=false;for(size_t i=0,j=polygon.size()-1;i<polygon.size();j=i++)if((polygon[i].y>y)!=(polygon[j].y>y)&&x<(polygon[j].x-polygon[i].x)*(y-polygon[i].y)/(polygon[j].y-polygon[i].y)+polygon[i].x)inside=!inside;if(inside)return true;}return false;
    }
};
inline PlayerIconShape playerIconShape(int id,bool playing,bool one){
    PlayerIconShape s;
    auto triangle=[&](float x,bool right,float width=.40f){s.fills.push_back({{x+(right?-width/2:width/2),.21f},{x+(right?-width/2:width/2),.79f},{x+(right?width/2:-width/2),.5f}});};
    if(id==1){if(playing){s.line(.36,.24,.36,.76,.13);s.line(.64,.24,.64,.76,.13);}else triangle(.53,true,.49);}
    else if(id==0||id==2){triangle(.50,id==2);s.line(id==2?.79f:.21f,.24,id==2?.79f:.21f,.76,.075);}
    else if(id==3){s.fills.push_back({{.26,.26},{.74,.26},{.74,.74},{.26,.74}});}
    else if(id==4){s.path({{.18,.28},{.33,.28},{.66,.72},{.82,.72}});s.path({{.18,.72},{.33,.72},{.66,.28},{.82,.28}});s.path({{.70,.17},{.83,.28},{.70,.39}});s.path({{.70,.61},{.83,.72},{.70,.83}});}
    else if(id==5){s.path({{.23,.44},{.23,.31},{.78,.31}});s.path({{.66,.18},{.80,.31},{.66,.44}});s.path({{.77,.56},{.77,.69},{.22,.69}});s.path({{.34,.56},{.20,.69},{.34,.82}});if(one)s.line(.5,.43,.5,.57,.055);}
    else if(id==6||id==7){triangle(.32,id==7,.30);triangle(.67,id==7,.30);}
    else if(id==8||id==9){s.path({{.17,.41},{.30,.41},{.47,.26},{.47,.74},{.30,.59},{.17,.59},{.17,.41}});s.line(.62,.50,.85,.50);if(id==9)s.line(.735,.38,.735,.62);}
    else if(id==10){for(int i=0;i<3;i++){float x=.26+i*.24f,y=i==1?.35f:.62f;s.line(x,.20,x,.80,.055);s.line(x-.075f,y,x+.075f,y,.11);}}
    else if(id==11){for(int x:{-1,1})for(int y:{-1,1}){float cx=.5+x*.29f,cy=.5+y*.29f;s.path({{cx-x*.17f,cy},{cx,cy},{cx,cy-y*.17f}},.065);}}
    else if(id==24){s.path({{.40,.14},{.60,.14},{.60,.40},{.86,.40},{.86,.60},{.60,.60},{.60,.86},{.40,.86},{.40,.60},{.14,.60},{.14,.40},{.40,.40},{.40,.14}},.045);}
    else if(id==25){s.line(.22,.50,.78,.50,.09);s.line(.50,.22,.50,.78,.09);}
    else if(id==26)s.line(.22,.50,.78,.50,.09);
    else if(id==27)s.circle(.5,.5,.40,.045);
    else s.box(.08,.22,.84,.56,.045);
    return s;
}
}
