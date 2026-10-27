#include "remote_view.hpp"
#include "mister_remote.hpp"
namespace nxh {
namespace {
using J = nlohmann::json;
J getPath(const J &doc, const std::string &path) {
    J value = doc;
    size_t off = 0;
    while (off < path.size()) {
        auto end = path.find('.', off);
        auto token = path.substr(off, end - off);
        if (!value.is_object() || !value.contains(token))
            return nullptr;
        value = value[token];
        if (end == std::string::npos)
            break;
        off = end + 1;
    }
    return value;
}
void putPath(J &doc, const std::string &path, J value) {
    J *out = &doc;
    size_t off = 0;
    while (true) {
        auto end = path.find('.', off);
        auto token = path.substr(off, end - off);
        if (end == std::string::npos) {
            (*out)[token] = std::move(value);
            break;
        }
        out = &(*out)[token];
        off = end + 1;
    }
}
J field(std::string key, std::string label, std::string kind = "text") {
    return {{"key", key}, {"label", label}, {"kind", kind}};
}
std::string display(const J &v) {
    if (v.is_boolean())
        return v.get<bool>() ? "On" : "Off";
    if (v.is_null())
        return "";
    return v.is_string() ? v.get<std::string>() : v.dump();
}
std::string timeText(double t) {
    int n = std::max(0, int(t));
    return std::to_string(n / 60) + ":" + (n % 60 < 10 ? "0" : "") + std::to_string(n % 60);
}
} // namespace
RemoteView::RemoteView(UI &ui, SDL_Renderer *r, RemoteClientConfig config, Keyboard keyboard,
                       std::vector<RemoteClientConfig> clients, bool screen)
    : ui_(ui), renderer_(r), client_(std::make_unique<ClientSession>(config, screen)),
      keyboard_(std::move(keyboard)), clients_(std::move(clients)),
      screenMode_(screen && config.type == "nx"), mister_(config.type == "mister") {
    status_ = "Connecting to " + config.name + "...";
    if (mister_) {
        loadMetadata();
        refreshSources();
    }
}
RemoteView::~RemoteView() {
    if (cover_)
        ui_.releaseCover(cover_);
}
ClientSession *RemoteView::detach() {
    if (client_)
        client_->stop();
    return client_.release();
}
uint64_t RemoteView::send(std::string method, std::string path, Json body,
                          std::function<void(const ClientSession::Result &)> cb) {
    auto id = client_->request(std::move(method), std::move(path), std::move(body));
    if (!id) {
        status_ = "Client is busy. Try again.";
        return 0;
    }
    if (cb)
        callbacks_[id] = std::move(cb);
    return id;
}
void RemoteView::loadMetadata() {
    lastSettings_ = SDL_GetTicks64();
    send("GET", "/api/settings/app", {}, [this](auto &r) {
        if (r.ok && r.data.is_object()) {
            appDoc_ = r.data;
            normalize();
            refreshSources();
        }
    });
}
void RemoteView::refreshSources() {
    if (mister_)
        send("GET", "/api/sources", {}, [this](auto &r) {
            if (r.ok && r.data.is_array()) {
                sources_ = r.data;
                if (!source_.empty()) {
                    bool found=false;
                    for (const auto &s:sources_) if(s.value("id",std::string{})==source_)found=true;
                    if(!found){source_.clear();path_.clear();entries_=Json::array();if(page_==1)changePage(0);}
                }
            }
        });
}
void RemoteView::normalize() {
    if (!mister_) {
        if (!raw_.is_object() || !raw_.contains("settings") || !raw_["settings"].is_object() ||
            !raw_.contains("playback") || !raw_["playback"].is_object() ||
            !raw_.contains("revision") || !raw_["revision"].is_number_integer() ||
            !raw_.contains("sources_revision") || !raw_["sources_revision"].is_number_integer()) {
            protocolReady_ = false;
            status_ = "The selected client is not exposing the NX Hi-Fi API.";
            return;
        }
        if (!protocolReady_)
            status_.clear();
        protocolReady_ = true;
        state_ = raw_;
        if (!state_.is_object() || !state_.contains("settings"))
            return;
        settings_ = state_["settings"];
        fields_ = state_.value("fields", Json::array());
        sources_ = Json::array();
        sources_.push_back({{"id", -1}, {"name", "SD card"}});
        int i = 0;
        for (auto &s : settings_.value("shares", Json::array()))
            sources_.push_back({{"id", i++}, {"name", s.value("name", std::string("SMB"))}});
        return;
    }
    if (!raw_.is_object() || !raw_.contains("state") || !raw_.contains("duration") ||
        !appDoc_.is_object() || !appDoc_.contains("config") || !appDoc_["config"].is_object())
        return;
    if (!protocolReady_)
        status_.clear();
    protocolReady_ = true;
    settings_ = {{"theme", "dark"},
                 {"accent_colour", "#EF4444"},
                 {"show_artwork", true},
                 {"visualizer", false},
                 {"show_clock", false},
                 {"volume", raw_.value("volume", 80)},
                 {"shuffle", raw_.value("shuffle", false)},
                 {"repeat", raw_.value("loop_mode", std::string{}) == "one" ? 2
                            : raw_.value("loop", false)                     ? 1
                                                                            : 0}};
    auto cfg = appDoc_.value("config", Json::object());
    fields_ = Json::array();
    for (auto f : appDoc_.value("fields", Json::array())) {
        auto path = f.value("path", std::string{});
        auto value = getPath(cfg, path);
        if (f.value("invert", false) && value.is_boolean())
            value = !value.get<bool>();
        auto k = "mister:" + path;
        settings_[k] = value;
        Json d = field(k, f.value("label", path));
        auto type = f.value("type", std::string("text"));
        d["group"] = misterSettingsCategory(f);
        d["help"] = f.value("help",std::string{});
        d["kind"] = type == "color" ? "colour" : (type == "bool" || type == "checkbox")      ? "bool"
                    : type == "select" || f.contains("options") ? "choice"
                    : type == "number" || type == "int"         ? "number"
                    : type == "password"                        ? "password"
                                                                : "text";
        d["min"] = f.value("min", -100000.0);
        d["max"] = f.value("max", 100000.0);
        d["step"] = f.value("step", 1.0);
        d["invert"] = f.value("invert", false);
        if (f.contains("options")) {
            d["choices"] = Json::array();
            d["labels"] = Json::array();
            for (auto &o : f["options"]) {
                d["choices"].push_back(o.is_object() ? o.value("value", Json{}) : o);
                d["labels"].push_back(o.is_object() ? o.value("label", display(o)) : display(o));
            }
        }
        fields_.push_back(d);
    }
    allFields_=fields_;
    fields_=Json::array();
    const char *categories[]={"Playback","Appearance","Controls","System","Experimental"};
    if(settingsCategory_>=0) for(const auto &f:allFields_) if(f.value("group",std::string{})==categories[settingsCategory_])fields_.push_back(f);
    settings_["accent_colour"]=cfg.value("accent_colour",std::string("#EF4444"));
    settings_["visualizer"]=true;
    settings_["muted"]=raw_.value("muted",false);
    settings_["theme"] = cfg.value("oled_mode", false) ? "oled" : "dark";
    settings_["show_artwork"] = !cfg.value("hide_album_art", false);
    settings_["hide_missing_art"] = cfg.value("auto_hide_missing_art", false);
    settings_["eq_enabled"] =
        getPath(cfg, "eq.enabled").is_boolean() ? getPath(cfg, "eq.enabled") : Json(false);
    settings_["eq"] = Json::array();
    for (auto k : {"Bass", "LowMid", "Mid", "HighMid", "Treble"}) {
        auto v = getPath(cfg, std::string("eq.") + k);
        settings_["eq"].push_back(v.is_number() ? v : Json(0));
    }
    Json p = {{"index", raw_.value("queue_index", 0)},
              {"count", raw_.value("queue_length", 0)},
              {"title", raw_.value("title", std::string("Nothing playing"))},
              {"artist", raw_.value("artist", std::string{})},
              {"album", raw_.value("album", std::string{})},
              {"position", raw_.value("position", 0.0)},
              {"duration", raw_.value("duration", 0.0)},
              {"playing", raw_.value("state", std::string{}) == "playing"},
              {"paused", raw_.value("state", std::string{}) == "paused"},
              {"busy", raw_.value("state", std::string{}) == "loading"},
              {"badges", Json::array()},
              {"spectrum", animatedRemoteSpectrum(SDL_GetTicks64()/1000.0,raw_.value("state",std::string{})=="playing")},
              {"loading_message",raw_.value("loading_message",std::string{})},
              {"error",raw_.value("error",std::string{})}};
    if (!raw_.value("format", std::string{}).empty())
        p["badges"].push_back(raw_["format"]);
    state_ = {{"settings", settings_},
              {"playback", p},
              {"clock", ""},
              {"has_artwork", raw_.value("has_art", false)},
              {"cover_revision", raw_.value("art_key", Json{})}};
}
void RemoteView::update() {
    if (screenCooldown_ > 0)
        --screenCooldown_;
    ClientSession::Result result;
    while (client_->poll(result)) {
        auto it = callbacks_.find(result.id);
        if (it != callbacks_.end()) {
            auto callback = std::move(it->second);
            callbacks_.erase(it);
            callback(result);
        }
        if (!result.ok)
            status_ = result.error;
    }
    auto oldSources = state_.value("sources_revision", Json{});
    auto next = client_->snapshot();
    if (next.is_object()) {
        raw_ = std::move(next);
        normalize();
        if (!mister_ && !oldSources.is_null() &&
            oldSources != state_.value("sources_revision", Json{})) {
            ++browseToken_;
            loading_ = false;
            entries_ = Json::array();
            source_.clear();
            path_.clear();
            positions_.clear();
            if (page_ == 1)
                changePage(0);
        }
    }
    if (screenMode_) {
        screenFrame_ = raw_.value("screen", Json::object());
        if (!screenFrame_.is_object())
            screenFrame_ = Json::object();
        auto now = SDL_GetTicks64();
        if (client_->connected() && !screenOpening_ &&
            (!screenHeartbeat_ || now - screenHeartbeat_ >= 2000)) {
            screenHeartbeat_ = now;
            if (screenToken_.empty()) {
                screenOpening_ = true;
                send("POST", "/api/cmd", {{"op", "screen_open"}}, [this](auto &r) {
                    screenOpening_ = false;
                    if (r.ok) {
                        screenToken_ = r.data.value("session", std::string{});
                        status_.clear();
                    } else
                        status_ = r.error;
                });
            } else
                send("POST", "/api/cmd", {{"op", "screen_keepalive"}, {"session", screenToken_}},
                     [this](auto &r) {
                         if (!r.ok) {
                             screenToken_.clear();
                             status_ = r.error;
                         }
                     });
        }
        if (screenFrame_.contains("keyboard") && !screenToken_.empty()) {
            auto prompt = screenFrame_["keyboard"];
            if (prompt.value("owner", std::string{}) == screenToken_ &&
                prompt.value("id", uint64_t(0)) != screenKeyboard_) {
                screenKeyboard_ = prompt.value("id", uint64_t(0));
                auto value = prompt.value("value", std::string{});
                bool accepted = keyboard_(prompt.value("label", std::string("Enter text")), value,
                                          prompt.value("secret", false));
                send("POST", "/api/cmd",
                     {{"op", "screen_text"},
                      {"session", screenToken_},
                      {"id", screenKeyboard_},
                      {"accepted", accepted},
                      {"value", value}});
                screenCooldown_ = 15;
            }
        }
    }
    if (mister_ && client_->connected() && SDL_GetTicks64() - lastSettings_ > 2000)
        loadMetadata();
    if (quickConnectPolling_ && client_->connected() && SDL_GetTicks64()-lastQuickPoll_>2000) {
        lastQuickPoll_=SDL_GetTicks64();
        send("GET","/api/jellyfin/quickconnect",{},[this](auto &r){
            if(!r.ok)return;
            auto state=r.data.value("state",std::string{});
            if(state=="pending")status_="Jellyfin Quick Connect: "+r.data.value("code",std::string{})+" - approve in Jellyfin";
            else {quickConnectPolling_=false;status_=state=="signed_in"?"Jellyfin signed in.":r.data.value("message",std::string("Sign-in ended."));refreshSources();}
        });
    }
    if (!state_.is_object())
        return;
    auto &artState = screenMode_ ? screenFrame_ : state_;
    std::string coverKey = artState.value("cover_revision", Json{}).dump();
    if (!artState.value("has_artwork", false))
        coverKey = "";
    if (coverKey != coverKey_) {
        coverKey_ = coverKey;
        if (cover_) {
            ui_.releaseCover(cover_);
            cover_ = nullptr;
        }
        if (!coverKey.empty()) {
            auto path = screenMode_ ? "/api/screen/art?r=" + urlEncode(coverKey)
                        : mister_ ? "/api/art?k=" + urlEncode(raw_.value("art_key", std::string{}))
                                  : "/api/art?revision=" + urlEncode(coverKey);
            send("GET", path, {}, [this, coverKey](auto &r) {
                if (r.ok && coverKey == coverKey_) {
                    std::vector<unsigned char> bytes(r.bytes.begin(), r.bytes.end());
                    if (cover_)
                        ui_.releaseCover(cover_);
                    cover_ = ui_.cover(bytes);
                }
            });
        }
    }
}
void RemoteView::command(std::string op, Json body) {
    if (!ready()) {
        status_ = "Waiting for the selected client to reconnect.";
        return;
    }
    if (!mister_) {
        if (op == "play") {
            op = "play_folder";
            body["track"] = body.at("path");
            body["path"] = path_;
        }
        body["op"] = op;
        send("POST", "/api/cmd", body);
        return;
    }
    if (op == "play") {
        send("POST", "/api/play", {{"source", source_}, {"path", body.at("path")}}, [this](auto &r){if(r.ok)status_.clear();});
        return;
    }
    if (op == "seek" || op == "seek_to") {
        double position = body.value("position", state_["playback"].value("position", 0.0) +
                                                     body.value("seconds", 0.0));
        send("POST", "/api/seek", {{"position", std::max(0.0, position)}});
        return;
    }
    if(op=="mute") {send("POST","/api/volume",{{"muted",!raw_.value("muted",false)}});return;}
    if (op == "volume") {
        send("POST", "/api/volume", {{"volume", body.at("value")}});
        return;
    }
    send("POST", "/api/control", {{"action", op == "repeat" ? "loop" : op}});
}
void RemoteView::patch(Json patch) {
    if (!ready())
        return;
    if (!mister_) {
        command("settings", {{"revision", state_.at("revision")}, {"patch", patch}});
        return;
    }
    Json out = Json::object();
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        auto key = it.key();
        if (key.substr(0, 7) == "mister:") {
            auto value = it.value();
            for (auto &f : fields_)
                if (f["key"] == key && f.value("invert", false) && value.is_boolean())
                    value = !value.get<bool>();
            putPath(out, key.substr(7), value);
        } else if (key == "eq_enabled")
            putPath(out, "eq.enabled", it.value());
        else if (key == "eq") {
            const char *names[] = {"Bass", "LowMid", "Mid", "HighMid", "Treble"};
            for (int i = 0; i < 5; ++i)
                putPath(out, std::string("eq.") + names[i], it.value()[i]);
        }
    }
    send("POST", "/api/settings/app", out, [this](auto &r) {
        if (r.ok)
            loadMetadata();
    });
}
void RemoteView::browse(std::string source, std::string path, std::string child) {
    if (!source_.empty() && !loading_)
        positions_[key()] = {selected_, scroll_, {}};
    auto previousSource=source_,previousPath=path_,previousRoot=root_,previousParent=parent_;
    auto previousEntries=entries_;int previousRow=selected_,previousScroll=scroll_;
    source_ = std::move(source);
    path_ = std::move(path);
    if (!child.empty())
        positions_[key()].child = std::move(child);
    page_ = 1;
    selected_ = scroll_ = 0;
    controls_ = false;
    loading_ = true;
    entries_ = Json::array();
    documentKind_.clear();
    auto token = ++browseToken_;
    auto response = [this, token,previousSource,previousPath,previousRoot,previousParent,previousEntries,previousRow,previousScroll](auto &r) {
        if (token != browseToken_)
            return;
        loading_ = false;
        if (!r.ok) {
            source_=previousSource;path_=previousPath;root_=previousRoot;parent_=previousParent;entries_=previousEntries;selected_=previousRow;scroll_=previousScroll;
            if(source_.empty())changePage(0);
            return;
        }
        entries_ = r.data.value("entries", Json::array());
        path_ = r.data.value("path", path_);
        root_ = r.data.value("root", std::string{});
        parent_=r.data.value("parent",std::string{});
        auto it = positions_.find(key());
        if (it != positions_.end()) {
            selected_ = it->second.row;
            scroll_ = it->second.scroll;
            for (int i = 0; i < int(entries_.size()); ++i)
                if (entries_[i].value("path", std::string{}) == it->second.child)
                    selected_ = i;
            selected_ = std::clamp(selected_, 0, maximum());
            scroll_ = std::clamp(scroll_, std::max(0, selected_ - layout().rows() + 1), selected_);
        }
        status_ = "";
    };
    if (mister_)
        send("GET", "/api/browse?source=" + urlEncode(source_) + "&path=" + urlEncode(path_), {},
             response);
    else
        send("POST", "/api/browse",
             {{"source", std::stoi(source_)},
              {"path", path_},
              {"sources_revision", state_.at("sources_revision")}},
             response);
}
void RemoteView::changePage(int page) {
    ++navigation_;
    dragging_ = false;
    page_ = page;
    selected_ = scroll_ = 0;
    controls_ = false;
    fullscreen_ = false;
    formOpen_ = false;
    documentKind_.clear();
    optionApply_ = nullptr;
    status_ = "";
}
int RemoteView::maximum() const {
    if (formOpen_)
        return int(formFields_.size()) + 1 + (testForm_ ? 1 : 0) + (formIndex_ >= 0 ? 1 : 0);
    if (!documentKind_.empty()) {
        if (documentKind_ == "plex")
            return 0;
        return int(
            document_.value(documentKind_ == "smb" ? "shares" : "stations", Json::array()).size());
    }
    if (page_ == 0)
        return int(sources_.size()) + (mister_ ? 4 : 0);
    if (page_ == 1)
        return std::max(0, int(entries_.size()) - 1);
    if (page_ == 2)
        return 7;
    if (page_ == 3) {
        if(mister_ && settingsCategory_<0)return 4;
        return std::max(0, int(fields_.size()) - 1);
    }
    return int(clients_.size());
}
void RemoteView::move(int delta) {
    if (optionApply_) {
        option_ = (option_ + delta % int(options_.size()) + int(options_.size())) % options_.size();
        return;
    }
    if (controls_ || fullscreen_) {
        seek(-delta * 5);
        return;
    }
    int count = maximum() + 1;
    selected_ = (selected_ + delta % count + count) % count;
    if (selected_ < scroll_)
        scroll_ = selected_;
    if (selected_ >= scroll_ + layout().rows())
        scroll_ = selected_ - layout().rows() + 1;
}
void RemoteView::adjust(int delta) {
    if (optionApply_) {
        move(delta);
        return;
    }
    if (controls_ || fullscreen_) {
        int count=mister_?11:10;
        control_ = (control_ + delta + count) % count;
        return;
    }
    if (page_ == 2 && !formOpen_ && selected_ >= 2 && selected_ <= 6) {
        auto eq = settings_.value("eq", Json::array({0, 0, 0, 0, 0}));
        eq[selected_ - 2] = std::clamp(eq[selected_ - 2].get<double>() + delta * .5, -6.0, 6.0);
        patch({{"eq", eq}, {"eq_enabled", true}});
        return;
    }
    move(delta * 5);
}
void RemoteView::seek(double seconds) {
    command("seek", {{"seconds", seconds}});
}
void RemoteView::transport(int index) {
    switch (index) {
    case 0:
        command("previous");
        break;
    case 1:
        command("playpause");
        break;
    case 2:
        command("next");
        break;
    case 3:
        command("stop");
        break;
    case 4:
        command("shuffle");
        break;
    case 5:
        command("repeat");
        break;
    case 6:
        seek(-10);
        break;
    case 7:
        seek(10);
        break;
    case 10:
        if(mister_)command("mute");
        break;
    case 8:
    case 9:
        command(
            "volume",
            {{"value", std::clamp(settings_.value("volume", 80) + (index == 8 ? -5 : 5), 0, 100)}});
        break;
    }
}
void RemoteView::choose(std::vector<Json> values, std::vector<std::string> labels, Json current,
                        std::function<void(Json)> callback) {
    if (values.empty())
        return;
    options_ = std::move(values);
    labels_ = std::move(labels);
    option_ = 0;
    for (int i = 0; i < int(options_.size()); ++i)
        if (options_[i] == current)
            option_ = i;
    optionApply_ = std::move(callback);
}
void RemoteView::editValue(const Json &f, Json value, std::function<void(Json)> cb) {
    std::string kind = f.value("kind", std::string("text"));
    if (kind == "info")return;
    if(kind=="action"){mediaServerLogin(f.value("server_kind",std::string{}));return;}
    if (kind == "bool") {
        cb(!value.get<bool>());
        return;
    }
    if (kind == "choice") {
        std::vector<Json> values;
        std::vector<std::string> labels;
        for (int i = 0; i < int(f["choices"].size()); ++i) {
            auto v = f["choices"][i];
            values.push_back(v);
            labels.push_back(f.contains("labels") ? f["labels"][i].get<std::string>() : display(v));
        }
        choose(values, labels, value, cb);
        return;
    }
    std::string text = kind == "password" ? "" : display(value);
    if (!keyboard_(f.value("label", std::string("Value")), text, kind == "password"))
        return;
    if (kind == "number" || kind == "port") {
        auto n = Json::parse(text, nullptr, false);
        if (!n.is_number() || n.get<double>() < f.value("min", kind == "port" ? 1.0 : -100000.0) ||
            n.get<double>() > f.value("max", kind == "port" ? 65535.0 : 100000.0)) {
            status_ = "Enter a number within the allowed range.";
            return;
        }
        if (value.is_number_integer() || kind == "port")
            n = int(n.get<double>());
        cb(n);
    } else if (kind == "colour") {
        if (!normalizeColour(text)) {
            status_ = "Enter a colour as #RRGGBB.";
            return;
        }
        cb(text);
    } else
        cb(text);
}
void RemoteView::action() {
    if (optionApply_) {
        auto callback = std::move(optionApply_);
        optionApply_ = nullptr;
        callback(options_[option_]);
        return;
    }
    if (page_ == 4 && !formOpen_ && !controls_ && !fullscreen_) {
        if (selected_ == 0)
            disconnect = true;
        else
            switchClient = selected_ - 1;
        return;
    }
    if (!ready()) {
        status_ = "Waiting for the selected client to reconnect.";
        return;
    }
    if (controls_ || fullscreen_) {
        transport(control_);
        return;
    }
    if (formOpen_) {
        int n = formFields_.size();
        if (selected_ < n) {
            auto f = formFields_[selected_];
            auto key = f.at("key").get<std::string>();
            editValue(f, form_.value(key, Json("")), [this, key](auto v) { form_[key] = v; });
        } else if (selected_ == n)
            saveForm_(false);
        else if (selected_ == n + 1) {
            formOpen_ = false;
            selected_ = scroll_ = 0;
        } else if (testForm_ && selected_ == n + 2)
            testForm_();
        else
            saveForm_(true);
        return;
    }
    if (!documentKind_.empty()) {
        documentForm(documentKind_, selected_);
        return;
    }
    if (page_ == 0) {
        if (selected_ < int(sources_.size())) {
            auto id = sources_[selected_]["id"];
            browse(id.is_string() ? id.get<std::string>() : id.dump(), "");
        } else if (!mister_)
            sourceEditor(-1);
        else {
            const char *kinds[]={"smb","radio","plex","jellyfin","emby"};
            editDocument(kinds[selected_-int(sources_.size())]);
        }
        return;
    }
    if (page_ == 1) {
        if (loading_ || entries_.empty())
            return;
        auto e = entries_[selected_];
        if(e.value("disabled",false)){status_="This file cannot be played. Select a game or song.";return;}
        if (e.value("directory", e.value("is_dir", false)))
            browse(source_, e["path"]);
        else {
            Json body = {{"path", e.at("path")}};
            if (!mister_) {
                body["source"] = std::stoi(source_);
                body["sources_revision"] = state_.at("sources_revision");
            }
            command("play", body);
        }
        return;
    }
    if (page_ == 2) {
        if (selected_ == 0)
            patch({{"eq_enabled", !settings_.value("eq_enabled", false)}});
        else if (selected_ == 1) {
            std::vector<Json> values;
            std::vector<std::string> labels;
            for (auto &p : presets) {
                values.push_back(p.gains);
                labels.push_back(p.name);
            }
            choose(values, labels, settings_.value("eq", Json{}),
                   [this](auto v) { patch({{"eq", v}, {"eq_enabled", true}}); });
        } else if (selected_ == 7)
            patch({{"eq", Json::array({0, 0, 0, 0, 0})}});
        return;
    }
    if(mister_ && page_==3 && settingsCategory_<0){settingsCategory_=selected_;selected_=scroll_=0;normalize();return;}
    if (page_ == 3 && selected_ < int(fields_.size())) {
        auto f = fields_[selected_];
        std::string key = f["key"];
        editValue(f, settings_.value(key, Json("")), [this, key](auto v) { patch({{key, v}}); });
    }
}
void RemoteView::back() {
    if (optionApply_) {
        optionApply_ = nullptr;
        return;
    }
    if (fullscreen_ || controls_) {
        fullscreen_ = controls_ = false;
        return;
    }
    if (formOpen_) {
        formOpen_ = false;
        if(documentKind_=="plex"||documentKind_=="jellyfin"||documentKind_=="emby")documentKind_.clear();
        selected_ = scroll_ = 0;
        return;
    }
    if (!documentKind_.empty()) {
        documentKind_.clear();
        selected_ = scroll_ = 0;
        return;
    }
    if(mister_ && page_==3 && settingsCategory_>=0){selected_=settingsCategory_;settingsCategory_=-1;scroll_=0;normalize();return;}
    if (page_ == 1 && !path_.empty() && path_ != "/" && path_ != root_) {
        auto old = path_;
        auto next = mister_ && !parent_.empty() ? parent_ : parent(path_);
        if (mister_ && (next.empty() || next.size() < root_.size()))
            next = root_.empty() ? "/" : root_;
        browse(source_, next, old);
    } else
        changePage(0);
}
void RemoteView::openForm(std::string title, Json values, Json fields,
                          std::function<void(bool)> save, std::function<void()> test) {
    formTitle_ = std::move(title);
    form_ = std::move(values);
    formFields_ = std::move(fields);
    saveForm_ = std::move(save);
    testForm_ = std::move(test);
    formOpen_ = true;
    selected_ = scroll_ = 0;
}
void RemoteView::sourceEditor(int index) {
    auto revision = state_.at("sources_revision");
    if (index == -2) {
        std::string root = settings_.value("music_root", std::string{});
        if (keyboard_("SD music folder", root, false))
            patch({{"music_root", root}});
        return;
    }
    formIndex_ = index;
    Json values = index < 0 ? Json{{"guest", false}} : settings_["shares"][index];
    values["password"] = "";
    Json fields = Json::array();
    for (auto k : {"name", "server", "share", "folder", "username", "password", "domain", "guest"})
        fields.push_back(field(k, k,
                               std::string(k) == "guest"      ? "bool"
                               : std::string(k) == "password" ? "password"
                                                              : "text"));
    openForm(
        index < 0 ? "ADD SMB SHARE" : "EDIT SMB SHARE", values, fields,
        [this, index, revision](bool remove) {
            Json body = {{"index", index}, {"sources_revision", revision}};
            if (!remove)
                body["share"] = form_;
            body["op"] = remove ? "source_remove" : "source_save";
            send("POST", "/api/cmd", body, [this](auto &r) {
                if (r.ok) {
                    formOpen_ = false;
                    selected_ = scroll_ = 0;
                }
            });
        },
        [this, index, revision] {
            send("POST", "/api/test",
                 {{"source", index}, {"sources_revision", revision}, {"share", form_}},
                 [this](auto &r) {
                     if (r.ok)
                         status_ = r.data.value("message", std::string("Connection successful."));
                 });
        });
}
void RemoteView::editDocument(std::string kind) {
    if (page_ != 0)
        return;
    auto navigation = navigation_;
    std::string endpoint = (kind == "plex" || kind=="jellyfin" || kind=="emby") ? "/api/"+kind+"/config" : "/api/settings/" + kind;
    send("GET", endpoint, {}, [this, kind, navigation](auto &r) {
        if (navigation != navigation_)
            return;
        if (!r.ok || !r.data.is_object())
            return;
        documentKind_ = kind;
        document_ = r.data;
        selected_ = scroll_ = 0;
        if(kind=="jellyfin"||kind=="emby")mediaServerForm(kind);
        else if (kind == "plex")documentForm(kind, 0);
    });
}
void RemoteView::documentForm(std::string kind, int index) {
    if (kind == "plex") {
        formIndex_ = -1;
        auto values = document_.value("config", document_);
        values["token"] = "";
        Json fields = Json::array();
        for (auto k : {"server", "token", "library", "insecure", "scrobbling", "max_track_mb"})
            fields.push_back(field(k, k,
                                   std::string(k) == "token"          ? "password"
                                   : std::string(k) == "insecure"     ? "bool"
                                   : std::string(k) == "max_track_mb" ? "number"
                                                                      : "text"));
        if (!values.contains("insecure"))
            values["insecure"] = false;
        if (!values.contains("max_track_mb")) values["max_track_mb"] = 128;
        for(auto &f:fields)if(f["key"]=="max_track_mb"){f["min"]=1;f["max"]=1024;}
        if (!values.contains("scrobbling") || values["scrobbling"] == "")
            values["scrobbling"] = "plex";
        for (auto &f : fields)
            if (f["key"] == "scrobbling") {
                f["kind"] = "choice";
                f["choices"] = Json::array({"plex", "lastfm", "listenbrainz", "both", "none"});
                f["labels"] = Json::array({"Plex", "Last.fm", "ListenBrainz", "Both", "Off"});
            }
        openForm(
            "PLEX", values, fields,
            [this](bool) {
                send("POST", "/api/plex/config", form_, [this](auto &r) {
                    if (r.ok) {
                        formOpen_ = false;
                        documentKind_.clear();
                        refreshSources();
                    }
                });
            },
            [this] {
                send("POST", "/api/settings/test", {{"kind", "plex"}, {"plex", form_}},
                     [this](auto &r) {
                         if (r.ok)
                             status_ = "Connection successful.";
                     });
            });
        return;
    }
    std::string list = kind == "smb" ? "shares" : "stations";
    auto rows = document_.value(list, Json::array());
    formIndex_ = index < int(rows.size()) ? index : -1;
    Json values = formIndex_ < 0 ? Json::object() : rows[index];
    Json fields = Json::array();
    if (kind == "smb") {
        if (!values.contains("guest"))
            values["guest"] = false;
        values["password"] = "";
        for (auto k : {"name", "server", "share", "path", "username", "password", "guest",
                       "clear_password"}) {
            if (std::string(k) == "clear_password")
                values[k] = false;
            fields.push_back(field(k, k,
                                   std::string(k) == "guest" || std::string(k) == "clear_password"
                                       ? "bool"
                                   : std::string(k) == "password" ? "password"
                                                                  : "text"));
        }
    } else {
        for (auto pair : {std::pair<const char *, const char *>{"name", "Name"},
                          {"url", "URL"},
                          {"genre", "Genre"}, {"logo", "Logo"}}) {
            if (values.contains(pair.second)) {
                values[pair.first] = values[pair.second];
                values.erase(pair.second);
            }
            fields.push_back(field(pair.first, pair.first));
        }
    }
    auto revision = document_.value("revision", Json{});
    openForm(
        kind == "smb" ? "SMB SHARE" : "RADIO STATION", values, fields,
        [this, kind, list, rows, revision, index](bool remove) mutable {
            auto collection = rows;
            if (remove) {
                if (formIndex_ >= 0)
                    collection.erase(index);
            } else if (formIndex_ < 0)
                collection.push_back(form_);
            else
                collection[index] = form_;
            send("POST", "/api/settings/" + kind, {{"revision", revision}, {list, collection}},
                 [this, kind](auto &r) {
                     if (r.ok) {
                         formOpen_ = false;
                         refreshSources();
                         editDocument(kind);
                     }
                 });
        },
        [this, kind, revision] {
            send("POST", "/api/settings/test",
                 {{"kind", kind}, {kind, form_}, {"revision", revision}}, [this](auto &r) {
                     if (r.ok)
                         status_ = "Connection successful.";
                 });
        });
}
void RemoteView::buttons(unsigned down, unsigned held) {
    if (screenMode_) {
        screenButtons(down, held);
        return;
    }
    if (down & Plus)
        command("playpause");
    if (down & ZL)
        command("previous");
    if (down & ZR)
        command("next");
    if (down & Y)
        command("shuffle");
    if (down & Minus) {
        if (fullscreen_) {
            fullscreen_ = controls_ = false;
        } else
            controls_ = !controls_;
    }
    if (down & Full) {
        fullscreen_ = !fullscreen_;
        controls_ = fullscreen_;
    }
    if (!optionApply_) {
        if (down & L)
            changePage((page_ + 4) % 5);
        if (down & R)
            changePage((page_ + 1) % 5);
    }
    if (held & (Up | Down)) {
        if (repeat_ == 0 || (repeat_ > 20 && repeat_ % 5 == 0))
            move(held & Up ? -1 : 1);
        ++repeat_;
    } else
        repeat_ = 0;
    if (held & (Left | Right)) {
        if (horizontal_ == 0 || (horizontal_ > 20 && horizontal_ % 6 == 0))
            adjust(held & Left ? -1 : 1);
        ++horizontal_;
    } else
        horizontal_ = 0;
    if (down & A)
        action();
    if (down & B)
        back();
    if (down & X) {
        if (page_ == 0 && !formOpen_ && !mister_ && selected_ < int(sources_.size()))
            sourceEditor(selected_ == 0 ? -2 : selected_ - 1);
        else
            command("stop");
    }
}
void RemoteView::displayChanged() {
    if (screenTouch_ && !screenToken_.empty())
        send("POST", "/api/cmd",
             {{"op", "screen_input"}, {"session", screenToken_}, {"kind", "cancel"}});
    screenTouch_ = false;
    dragging_ = false;
    layout().keepVisible(selected_, scroll_, maximum() + 1);
}
void RemoteView::swipe(int delta) {
    if (!optionApply_ && !controls_ && !fullscreen_)
        move(delta);
}
bool RemoteView::beginScrub(int x, int y) {
    if (screenMode_)
        return false;
    if (!ready() || optionApply_)
        return false;
    if (!((fullscreen_ &&
           inside(x, y, playerX() - 10, layout().fullY(510) - 15, playerWidth() + 20, 40)) ||
          (!fullscreen_ && inside(x, y, 890, layout().bottom(526), 348, 40))))
        return false;
    if (state_.value("playback", Json::object()).value("duration", 0.0) <= 0)
        return false;
    dragging_ = true;
    previewScrub(x);
    return true;
}
void RemoteView::previewScrub(int x) {
    if (!dragging_)
        return;
    double duration = state_.value("playback", Json::object()).value("duration", 0.0);
    scrubPosition_ = std::clamp((x - playerX()) / double(playerWidth()), 0.0, 1.0) * duration;
}
void RemoteView::drag(int x) {
    if (!dragging_)
        return;
    previewScrub(x);
    command("seek_to", {{"position", scrubPosition_}});
    dragging_ = false;
}
void RemoteView::click(int x, int y) {
    if (screenMode_) {
        screenPointer("up", x, y);
        return;
    }
    if (optionApply_) {
        int first = layout().optionFirst(option_);
        if (inside(x, y, 170, 170, 940, layout().optionRows() * 48)) {
            int i = first + (y - 170) / 48;
            if (i < int(options_.size())) {
                option_ = i;
                action();
            }
        }
        return;
    }
    if (!fullscreen_ && inside(x, y, 28, 90, 770, 48)) {
        changePage(std::clamp((x - 28) / 154, 0, 4));
        return;
    }
    if (inside(x, y, 28, layout().bottom(635), 740, 48)) {
        controls_ = true;
        control_ = (x - 28) / 74;
        transport(control_);
        return;
    }
    if (inside(x, y, 804, layout().bottom(635), 66, 46)) {
        fullscreen_ = !fullscreen_;
        controls_ = fullscreen_;
        return;
    }
    if(mister_ && inside(x,y,888,layout().bottom(635),76,46)){control_=10;controls_=true;transport(10);return;}
    if (inside(x, y, 980, layout().bottom(635), 250, 48)) {
        command("volume", {{"value", std::clamp((x - 980) * 100 / 250, 0, 100)}});
        return;
    }
    if ((fullscreen_ &&
         inside(x, y, playerX() - 10, layout().fullY(510) - 15, playerWidth() + 20, 40)) ||
        (!fullscreen_ && inside(x, y, 890, layout().bottom(526), 348, 40))) {
        dragging_ = true;
        drag(x);
        return;
    }
    if (!fullscreen_ && inside(x, y, 28, 182, 828, layout().rows() * 48)) {
        int target = scroll_ + (y - 182) / 48;
        if (target <= maximum()) {
            controls_ = false;
            selected_ = target;
            action();
        }
    }
}
void RemoteView::drawRows(std::vector<std::string> names, std::vector<std::string> values,
                          std::string heading) {
    ui_.text(46, 160, heading, 15, MUTED, 790);
    for (int i = scroll_; i < std::min(scroll_ + layout().rows(), int(names.size())); ++i) {
        int y = 182 + (i - scroll_) * 48;
        bool active = i == selected_ && !controls_;
        if (active)
            ui_.rect(38, y, 808, 44, FOCUS);
        ui_.text(50, y + 10, names[i], 20, active ? ACCENT : TEXT, values.empty() ? 780 : 440);
        if (i < int(values.size()))
            ui_.text(505, y + 12, values[i], 17, MUTED, 325);
    }
}
void RemoteView::drawPlayer() {
    auto p = state_.value("playback", Json::object());
    bool art = artworkVisible();
    int x = playerX(), width = playerWidth();
    if (art)
        ui_.image(cover_, fullscreen_ ? 64 : layout().compactArtX(), fullscreen_ ? 100 : 178,
                  fullscreen_ ? 440 : layout().compactArtDesignSize());
    int y = fullscreen_ ? layout().fullY(155) : (art ? layout().compactTitleY() : 178);
    ui_.marquee(x, y, p.value("title", std::string("Nothing playing")), fullscreen_ ? 36 : 25, TEXT,
                width, "remote-title");
    ui_.text(x, (fullscreen_ ? layout().fullY(228) : y + 39), p.value("artist", std::string{}),
             fullscreen_ ? 28 : 17, ACCENT, width);
    if (fullscreen_ || layout().docked)
        ui_.text(x, (fullscreen_ ? layout().fullY(280) : y + 74), p.value("album", std::string{}),
                 fullscreen_ ? 24 : 17, MUTED, width);
    {
        int size = fullscreen_ ? 18 : 13;
        int bx = x, by = fullscreen_ ? layout().fullY(340) : (y + 67);
        // Compact album and badges have dedicated rows on the larger TV layout.
        if (!fullscreen_ && layout().docked)
            by = y + 108;
        for (auto &v : p.value("badges", Json::array())) {
            auto label = display(v);
            int w = ui_.textWidth(label, size) + 20;
            if (bx != x && bx + w > x + width) {
                bx = x;
                by += size + 16;
            }
            ui_.badge(bx, by, w, size + 14, label, size);
            bx += w + 8;
        }
    }
    if (fullscreen_ && p.value("count", 0) > 0)
        ui_.text(x, layout().fullY(392),
                 std::to_string(p.value("index", 0) + 1) + " / " +
                     std::to_string(p.value("count", 0)),
                 17, MUTED);
    if (settings_.value("visualizer", false)) {
        auto bars = p.value("spectrum", Json::array());
        for (int i = 0; i < std::min(32, int(bars.size())); ++i) {
            int h = std::max(
                2, int(std::clamp(bars[i].get<double>(), 0.0, 1.0) * (fullscreen_ ? 64 : 24)));
            ui_.rect(x + i * width / 32,
                     (fullscreen_ ? layout().fullY(491) : layout().bottom(530)) - h,
                     std::max(3, width / 32 - 7), h, ACCENT);
        }
    }
    int sy = fullscreen_ ? layout().fullY(510) : layout().bottom(544);
    ui_.rect(x, sy, width, 6, LINE);
    double duration = p.value("duration", 0.0),
           position = dragging_ ? scrubPosition_ : p.value("position", 0.0);
    if (duration > 0)
        ui_.rect(x, sy, int(width * std::clamp(position / duration, 0.0, 1.0)), 6, ACCENT);
    ui_.text(x, sy + 25, timeText(position) + " / " + timeText(duration), 18, MUTED, width);
}
void RemoteView::drawControls() {
    auto p = state_.value("playback", Json::object());
    ui_.text(28, layout().bottom(605), client_->connected() ? (!status_.empty()?status_:p.value("error",std::string{}).empty()?p.value("loading_message",std::string{}):p.value("error",std::string{})) : client_->error(), 16,
             ACCENT, 1220);
    ui_.rect(0, layout().bottom(630), 1280, 90, FOOTER);
    for (int i = 0; i < 10; ++i) {
        bool focus = (controls_ || fullscreen_) && i == control_;
        ui_.rect(28 + i * 74, layout().bottom(635), 66, 46, focus ? FOCUS : PANEL);
        if (focus)
            ui_.rect(28 + i * 74, layout().bottom(679), 66, 2, ACCENT);
        bool active = (i == 4 && settings_.value("shuffle", false)) ||
                      (i == 5 && settings_.value("repeat", 0));
        ui_.transportIcon(61 + i * 74, layout().bottom(658), 24, i,
                          p.value("playing", false) && !p.value("paused", false),
                          active ? ACCENT : TEXT, settings_.value("repeat", 0) == 2);
    }
    ui_.rect(804, layout().bottom(635), 66, 46, PANEL);
    for (int side : {-1, 1})
        for (int vertical : {-1, 1}) {
            int x = 837 + side * 12, y = layout().bottom(658) + vertical * 12;
            ui_.line(x, y, x - side * 7, y, 2, TEXT);
            ui_.line(x, y, x, y - vertical * 7, 2, TEXT);
        }
    if(mister_)ui_.button(888,layout().bottom(635),76,46,settings_.value("muted",false)?"Unmute":"Mute",(controls_||fullscreen_)&&control_==10);
    ui_.text(980, layout().bottom(635),
             "Volume " + std::to_string(settings_.value("volume", 80)) + "%", 15, MUTED);
    ui_.rect(980, layout().bottom(669), 250, 5, LINE);
    ui_.rect(980, layout().bottom(669), settings_.value("volume", 80) * 250 / 100, 5, ACCENT);
    std::string hint = optionApply_ ? "Up/Down Choose   A Confirm   B Cancel"
                       : controls_ || fullscreen_
                           ? "Left/Right Controls   Up/Down Seek 5s   A Use   - Browse"
                           : "A Open/Use   B Back   Left/Right Jump 5   - Controls";
    ui_.text(28, layout().bottom(692),
             hint + "   L/R Tabs   R3 Player   ZL/ZR Prev/Next   + Play/Pause", 13, MUTED, 1224);
}
void RemoteView::draw() {
    if (screenMode_) {
        drawScreen();
        return;
    }
    setAppearance(settings_.value("theme", std::string("dark")),
                  settings_.value("accent_colour", std::string("#EF4444")));
    ui_.begin();
    ui_.text(28, 19, "NX Hi-Fi", 38, ACCENT);
    ui_.text(28 + ui_.textWidth("NX Hi-Fi", 38) + 14, 35, std::string("v") + Version, 16, MUTED);
    ui_.text(375, 30, "Independent · " + client_->config.name, 22, ACCENT, 580);
    if (settings_.value("show_clock", false))
        ui_.text(1110, 29, state_.value("clock", std::string{}), 22, TEXT, 142);
    if (!fullscreen_) {
        const char *tabs[] = {"Sources", "Library", "Equalizer", "Settings", "Remote"};
        for (int i = 0; i < 5; ++i)
            ui_.button(28 + i * 154, 90, 142, 48, tabs[i], page_ == i);
        ui_.rect(28, 155, 828, layout().panelHeight(), PANEL);
        ui_.rect(876, 155, 376, layout().panelHeight(), PANEL);
        std::vector<std::string> names, values;
        std::string heading;
        if (formOpen_) {
            heading = formTitle_;
            for (auto &f : formFields_) {
                auto k = f.at("key").get<std::string>();
                names.push_back(f.value("label", k));
                values.push_back(f.value("kind", std::string{}) == "password"
                                     ? "Blank keeps saved secret"
                                     : display(form_.value(k, Json{})));
            }
            names.push_back("Save");
            names.push_back("Cancel");
            if (testForm_)
                names.push_back("Test connection");
            if (formIndex_ >= 0)
                names.push_back("Remove");
        } else if (!documentKind_.empty()) {
            heading = documentKind_ + " SETTINGS";
            auto rows =
                document_.value(documentKind_ == "smb" ? "shares" : "stations", Json::array());
            for (auto &r : rows)
                names.push_back(r.value("name", r.value("Name", std::string("Entry"))));
            names.push_back("+ Add entry");
        } else if (page_ == 0) {
            heading = "SOURCES";
            for (auto &s : sources_)
                names.push_back(s.value("name", std::string("Source")));
            if (mister_) {
                names.push_back("Manage SMB shares");
                names.push_back("Manage radio stations");
                names.push_back("Configure Plex");
                names.push_back("Configure Jellyfin");
                names.push_back("Configure Emby");
            } else
                names.push_back("+ Add SMB share");
        } else if (page_ == 1) {
            heading = loading_ ? "LOADING..." : path_.empty() ? "LIBRARY" : path_;
            for (auto &e : entries_)
                names.push_back((e.value("directory", e.value("is_dir", false)) ? "> " : "") +
                                e.value("name", std::string{}));
        } else if (page_ == 2) {
            heading = "FIVE-BAND EQUALIZER";
            names = {"Equalizer", "Preset",         "Bass 60 Hz",    "Low-mid 250 Hz",
                     "Mid 1 kHz", "High-mid 4 kHz", "Treble 12 kHz", "Reset flat"};
            values.push_back(settings_.value("eq_enabled", false) ? "On" : "Off");
            std::string preset = "Custom";
            auto bands = settings_.value("eq", Json::array({0, 0, 0, 0, 0}));
            for (auto &p : presets)
                if (bands == Json(p.gains))
                    preset = p.name;
            values.push_back(preset);
            for (auto &v : bands)
                values.push_back(display(v) + " dB");
        } else if (page_ == 3) {
            const char *categories[]={"Playback","Appearance","Controls","System","Experimental"};
            heading = mister_ && settingsCategory_>=0 ? std::string("MISTER SETTINGS / ")+categories[settingsCategory_] : "TARGET SETTINGS";
            if(mister_ && settingsCategory_<0){for(auto category:categories)names.push_back(category);}
            for (auto &f : fields_) {
                auto k = f.at("key").get<std::string>();
                names.push_back(f.value("label", k));
                values.push_back(f.value("kind", std::string{}) == "password"
                                     ? "Blank keeps saved secret"
                                     : display(k == "remote_address"
                                                   ? state_.value("remote_address", Json(""))
                                                   : settings_.value(k, Json{})));
            }
        } else {
            heading = "REMOTE CLIENT";
            names.push_back("Disconnect and return to local");
            for (auto &c : clients_)
                names.push_back("Connect · " + c.name);
        }
        drawRows(names, values, heading);
    }
    drawPlayer();
    drawControls();
    if (optionApply_) {
        ui_.rect(140, 100, 1000, 510 + layout().extraHeight(), PANEL);
        int first = layout().optionFirst(option_);
        ui_.text(170, 120, "SELECT OPTION", 26, ACCENT);
        for (int i = first; i < std::min(first + layout().optionRows(), int(labels_.size())); ++i)
            ui_.button(170, 170 + (i - first) * 48, 940, 42, labels_[i], i == option_);
    }
    SDL_RenderPresent(renderer_);
}

void RemoteView::screenMenuAction() {
    if (screenMenuSelection_ == 0)
        disconnect = true;
    if (screenMenuSelection_ == 1)
        changeMode = true;
    if (screenMenuSelection_ == 2)
        screenMenu_ = false;
}
void RemoteView::screenButtons(unsigned down, unsigned held) {
    if (screenCooldown_ > 0)
        return;
    int minus = minusHold_.update(held & Minus, SDL_GetTicks64());
    if (minus == 2) {
        screenMenu_ = true;
        screenMenuSelection_ = 0;
    }
    if (screenMenu_) {
        if (down & Up)
            screenMenuSelection_ = (screenMenuSelection_ + 2) % 3;
        if (down & Down)
            screenMenuSelection_ = (screenMenuSelection_ + 1) % 3;
        if (down & A)
            screenMenuAction();
        if (down & B)
            screenMenu_ = false;
        return;
    }
    unsigned buttons = down & ~Minus;
    if (minus == 1)
        buttons |= Minus;
    unsigned dir = held & (Up | Down | Left | Right);
    if (dir) {
        if (repeat_ == 0 || (repeat_ > 20 && repeat_ % 6 == 0))
            buttons |= dir;
        ++repeat_;
    } else
        repeat_ = 0;
    if (buttons && client_->connected() && !screenToken_.empty())
        send("POST", "/api/cmd",
             {{"op", "screen_input"},
              {"session", screenToken_},
              {"kind", "buttons"},
              {"buttons", buttons}});
}
void RemoteView::screenPointer(const std::string &kind, int x, int y) {
    if (kind == "up" && inside(x, y, 1060, 18, 192, 44)) {
        disconnect = true;
        return;
    }
    if (screenMenu_) {
        if (kind == "up")
            for (int i = 0; i < 3; ++i)
                if (inside(x, y, 230, 260 + i * 70, 820, 56)) {
                    screenMenuSelection_ = i;
                    screenMenuAction();
                    return;
                }
        return;
    }
    if (!client_->connected() || screenToken_.empty())
        return;
    auto now = SDL_GetTicks64();
    if (kind == "move" && now - screenMotion_ < 40)
        return;
    screenMotion_ = now;
    int tx = 0, ty = 0;
    if (!ui_.screenPoint(screenFrame_, x, y, tx, ty)) {
        if (kind != "up" || !screenTouch_)
            return;
        tx = screenTouchX_;
        ty = screenTouchY_;
    }
    if (kind == "down")
        screenTouch_ = true;
    if (!screenTouch_)
        return;
    screenTouchX_ = tx;
    screenTouchY_ = ty;
    if (kind == "up")
        screenTouch_ = false;
    send("POST", "/api/cmd",
         {{"op", "screen_input"},
          {"session", screenToken_},
          {"kind", kind},
          {"x", tx},
          {"y", ty},
          {"w", screenFrame_.value("w", 0)},
          {"h", screenFrame_.value("h", 0)}});
}
void RemoteView::drawScreen() {
    setAppearance(settings_.value("theme", std::string("dark")),
                  settings_.value("accent_colour", std::string("#EF4444")));
    ui_.begin();
    ui_.text(28, 25, "Screen control · " + client_->config.name, 22, ACCENT, 980);
    ui_.button(1060, 18, 192, 44, "Disconnect");
    try {
        ui_.replayScreen(screenFrame_, cover_);
    } catch (...) {
        status_ = "Waiting for a valid screen update.";
    }
    if (screenFrame_.empty() || !client_->connected() || !status_.empty()) {
        ui_.rect(0, layout().height() - 48, 1280, 48, BG);
        ui_.text(44, layout().height() - 40,
                 !client_->connected()
                     ? "Remote client is offline. Hold Minus for 3 seconds for the local menu."
                 : status_.empty() ? "Opening remote screen..."
                                   : status_,
                 17, ACCENT, 1180);
    }
    if (screenMenu_) {
        ui_.rect(200, 170, 880, 355, PANEL);
        ui_.text(230, 200, "REMOTE CONTROL", 25, ACCENT);
        const char *names[] = {"Disconnect", "Switch mode", "Cancel"};
        for (int i = 0; i < 3; ++i)
            ui_.button(230, 260 + i * 70, 820, 56, names[i], screenMenuSelection_ == i);
    }
    SDL_RenderPresent(renderer_);
}
} // namespace nxh
