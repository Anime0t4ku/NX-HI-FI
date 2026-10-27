#pragma once
#include "client.hpp"
#include "ui.hpp"
#include "screen_control.hpp"
#include <functional>
#include <map>
namespace nxh {
class RemoteView {
  public:
    enum Button : unsigned {
        A = 1,
        B = 2,
        X = 4,
        Y = 8,
        Minus = 16,
        Plus = 32,
        L = 64,
        R = 128,
        ZL = 256,
        ZR = 512,
        Up = 1024,
        Down = 2048,
        Left = 4096,
        Right = 8192,
        Full = 16384
    };
    using Json = nlohmann::json;
    using Keyboard = std::function<bool(const std::string &, std::string &, bool)>;
    RemoteView(UI &, SDL_Renderer *, RemoteClientConfig, Keyboard, std::vector<RemoteClientConfig>,
               bool screen = false);
    ~RemoteView();
    ClientSession *detach();
    bool disconnect = false, changeMode = false;
    bool screenMode() const {
        return screenMode_;
    }
    const RemoteClientConfig &config() const {
        return client_->config;
    }
    const std::string &screenToken() const {
        return screenToken_;
    }
    void screenPointer(const std::string &kind, int x, int y);

    int switchClient = -1;
    void update();
    void draw();
    void displayChanged();
    void buttons(unsigned down, unsigned held);
    void click(int x, int y);
    void drag(int x);
    bool beginScrub(int x, int y);
    void previewScrub(int x);
    void swipe(int delta);
    bool full() const {
        return fullscreen_;
    }
    bool scrubActive() const {
        return dragging_;
    }

  private:
    const Layout &layout() const {
        return ui_.layout();
    }
    UI &ui_;
    SDL_Renderer *renderer_;
    std::unique_ptr<ClientSession> client_;
    Keyboard keyboard_;
    std::vector<RemoteClientConfig> clients_;
    bool screenMode_ = false, screenMenu_ = false, screenOpening_ = false;
    int screenMenuSelection_ = 0, screenCooldown_ = 0;
    MinusHold minusHold_;
    std::string screenToken_;
    Json screenFrame_ = Json::object();
    bool screenTouch_ = false;
    int screenTouchX_ = 0, screenTouchY_ = 0;
    uint64_t screenHeartbeat_ = 0, screenKeyboard_ = 0, screenMotion_ = 0;
    void screenButtons(unsigned down, unsigned held);
    void drawScreen();
    void screenMenuAction();
    bool protocolReady_ = false;
    bool mister_, fullscreen_ = false, controls_ = false, loading_ = false, dragging_ = false;
    int page_ = 0, selected_ = 0, scroll_ = 0, control_ = 1, repeat_ = 0, horizontal_ = 0;
    Json state_ = Json::object(), raw_ = Json::object(), settings_ = Json::object(),
         fields_ = Json::array(), appDoc_ = Json::object(), sources_ = Json::array(),
         entries_ = Json::array();
    std::string source_, path_, root_, status_, coverKey_;
    SDL_Texture *cover_ = nullptr;
    double scrubPosition_ = 0;
    uint64_t lastSettings_ = 0, browseToken_ = 0, navigation_ = 0;
    struct Position {
        int row = 0, scroll = 0;
        std::string child;
    };
    std::map<std::string, Position> positions_;
    std::map<uint64_t, std::function<void(const ClientSession::Result &)>> callbacks_;
    std::vector<Json> options_;
    std::vector<std::string> labels_;
    std::function<void(Json)> optionApply_;
    int option_ = 0;
    bool formOpen_ = false;
    Json form_, formFields_, document_;
    std::string formTitle_;
    int formIndex_ = -1;
    std::string documentKind_;
    std::function<void(bool)> saveForm_;
    std::function<void()> testForm_;
    bool ready() const {
        return protocolReady_ && client_ && client_->connected() && state_.is_object();
    }
    uint64_t send(std::string method, std::string path, Json body = Json::object(),
                  std::function<void(const ClientSession::Result &)> cb = {});
    void command(std::string op, Json body = Json::object());
    void patch(Json);
    void loadMetadata();
    void normalize();
    void refreshSources();
    void browse(std::string source, std::string path, std::string child = "");
    void changePage(int);
    void move(int);
    void adjust(int);
    void action();
    void back();
    int maximum() const;
    void transport(int);
    void seek(double);
    void choose(std::vector<Json>, std::vector<std::string>, Json, std::function<void(Json)>);
    void editValue(const Json &field, Json value, std::function<void(Json)>);
    void sourceEditor(int index);
    void editDocument(std::string kind);
    void documentForm(std::string kind, int index);
    void openForm(std::string, Json, Json, std::function<void(bool)>,
                  std::function<void()> test = {});
    void drawRows(std::vector<std::string>, std::vector<std::string>, std::string);
    bool artworkVisible() const {
        return settings_.value("show_artwork", true) &&
               (cover_ || !settings_.value("hide_missing_art", false));
    }
    int playerX() const {
        return fullscreen_ ? (artworkVisible() ? 560 : 64) : 900;
    }
    int playerWidth() const {
        return fullscreen_ ? (artworkVisible() ? 656 : 1152) : 328;
    }
    void drawPlayer();
    void drawControls();
    std::string key() const {
        return source_ + "\n" + path_;
    }
};
} // namespace nxh
