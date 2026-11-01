// Local fullscreen visualizer and quick settings panel.
nlohmann::json remoteSettingFields()const{
    return settingFields();
}
Visualizer visualizer;
bool visualizerFullscreen=false,playerSettingsOpen=false;
int playerSettingsTab=0,playerSettingsRow=0;
PlayerRect visualizerRect()const{return {playerX(),370,playerWidth(),std::max(8,seekRect().y-380)};}
void cycleVisualizer(int dir){auto it=std::find(visualizerIds.begin(),visualizerIds.end(),settings.visualizerStyle);int i=it==visualizerIds.end()?0:int(it-visualizerIds.begin());applySettings({{"visualizer_style",visualizerIds[(i+dir+int(visualizerIds.size()))%visualizerIds.size()]}});}
bool canExpandVisualizer()const{return fullscreen&&settings.visualizer&&settings.visualizerExpand&&!listening;}
void visualizerChoice(const std::string &key){
    for(auto &f:settingFields())if(f["key"]==key){
        std::vector<nlohmann::json> values;std::vector<std::string> labels;
        for(auto &v:f["choices"]){values.push_back(v);labels.push_back(f.contains("choice_labels")?f["choice_labels"][labels.size()].template get<std::string>():std::to_string(v.template get<int>())+"%");}
        choose(f["label"],values,labels,settingsDocument(settings)[key],[this,key](auto v){applySettings({{key,v}});});return;
    }
}
void playerSettingsChange(int dir=0){
    if(playerSettingsRow<0)playerSettingsRow=0;
    if(playerSettingsTab==1){
        switch(playerSettingsRow){
        case 0:applySettings({{"visualizer",!settings.visualizer}});break;
        case 1:visualizerChoice("visualizer_style");break;
        case 2:visualizerChoice("visualizer_sensitivity");break;
        case 3:visualizerChoice("visualizer_smoothing");break;
        case 4:applySettings({{"visualizer_expand",!settings.visualizerExpand}});break;
        case 5:colourChoices("visualizer_colour");break;
        }return;
    }
    if(playerSettingsRow==0)applySettings({{"eq_enabled",!settings.eqEnabled}});
    else if(playerSettingsRow==1)visualizerChoice("eq_preset");
    else if(playerSettingsRow<7){int band=playerSettingsRow-2;applySettings({{"eq_"+std::to_string(band),std::clamp(settings.eq[band]+(dir?dir:1)*.5f,-6.f,6.f)}});}
    else applySettings({{"eq_reset",true}});
}
void drawPlayerSettings(){
    if(!playerSettingsOpen)return;
    ui.roundedRect(140,100,1000,510+layout().extraHeight(),{PANEL.r,PANEL.g,PANEL.b,250},18);
    ui.button(168,118,454,46,"Equalizer",playerSettingsTab==0);
    ui.button(638,118,474,46,"Visualizer",playerSettingsTab==1);
    std::vector<std::string> names,values;
    if(playerSettingsTab==0){
        names={"Equalizer","Preset","Bass 60 Hz","Low-mid 250 Hz","Mid 1 kHz","High-mid 4 kHz","Treble 12 kHz","Reset gains"};
        std::string preset="Custom";for(auto &p:presets)if(settings.eq==p.gains)preset=p.name;
        values={settings.eqEnabled?"On":"Off",preset};for(auto gain:settings.eq){char text[24];std::snprintf(text,sizeof(text),"%+.1f dB",gain);values.push_back(text);}values.push_back("");
    }else{
        names={"Show visualizer","Fullscreen style","Sensitivity","Smoothing","Select to expand","Colour"};
        values={settings.visualizer?"On":"Off",visualizerName(settings.visualizerStyle),std::to_string(settings.visualizerSensitivity)+"%",std::to_string(settings.visualizerSmoothing)+"%",settings.visualizerExpand?"On":"Off",settings.visualizerColour=="theme"?"Theme colour":settings.visualizerColour};
    }
    for(int i=0;i<int(names.size());i++){
        int y=198+i*43;ui.button(170,y,940,39,"",playerSettingsRow==i);ui.text(188,y+9,names[i],17,TEXT,420);ui.rightText(1088,y+9,values[i],17,ACCENT);
    }
    ui.centeredHints(565+layout().extraHeight(),{{"L",""},{"R","Tabs"},{"Dpad","Adjust"},{"A","Choose"},{"B","Close"}},140,1000);
}
void drawLocalVisualizer(const Playback &p){
    // The remote frame has stock bars and the normal player, even during expansion.
    if(!fullscreen||!settings.visualizer)return;
    Settings renderSettings=settings;if(independent)renderSettings.visualizerStyle=0;
    bool recording=ui.recordingScreen();ui.recordScreen(false);
    if(visualizerFullscreen){
        visualizer.draw(ui,p,{0,0,1280,layout().height()},renderSettings);
    }else{
        auto a=visualizerRect();if(canExpandVisualizer()&&fullFocus.row==-1&&!playerSettingsOpen&&!optionApply)ui.rect(a.x,a.y,a.w,a.h,FOCUS);visualizer.draw(ui,p,a,renderSettings);
    }
    ui.recordScreen(recording);
}
