// All state below is owned by the UI thread; network jobs return data only.
ClientScanner scanner;
std::atomic<bool> probeCancel{false};
std::future<ProbeResult> probeTask;
RemoteClientConfig pendingClient;
bool connectionOpen = false, scanOpen = false, canScreen = false;
int connectionStage = 0, connectionSelection = 0, scanSelection = 0;
ScreenSessions screenSessions;
nlohmann::json keyboardReplay;
Uint64 lastScreenPublish = 0;
bool screenPointerActive = false, screenPointerGesture = false;
int screenPointerY = 0;
void beginConnect(RemoteClientConfig config) {
    if (probeTask.valid() &&
        probeTask.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    if (probeTask.valid())
        probeTask.get();
    scanner.cancel();
    probeCancel = false;
    pendingClient = std::move(config);
    pendingClient.port = 8182;
    connectionOpen = true;
    connectionStage = 0;
    connectionSelection = 0;
    scanOpen = false;
    auto target = pendingClient;
    probeTask =
        std::async(std::launch::async, [this, target] { return probeRemote(target, probeCancel); });
}
void closeConnection() {
    connectionOpen = false;
    probeCancel = true;
}
void activateRemote(bool screen) {
    connectionOpen = false;
    disconnectTarget();
    targetView = std::make_unique<RemoteView>(
        ui, renderer, pendingClient,
        [](const auto &label, auto &value, bool secret) { return keyboard(label, value, secret); },
        settings.remoteClients, screen);
    cooldown = 15;
}
void connectionAction() {
    if (connectionStage == 0) {
        closeConnection();
        return;
    }
    if (connectionStage == 1) {
        if (connectionSelection == 0)
            beginConnect(pendingClient);
        else
            closeConnection();
        return;
    }
    if (connectionSelection == 0)
        activateRemote(false);
    else if (connectionSelection == 1 && canScreen)
        activateRemote(true);
    else if (connectionSelection == 2)
        closeConnection();
}
void startScan() {
    scanOpen = true;
    scanSelection = 0;
    scanner.start(localSubnetHosts());
}
void scanAction() {
    auto found = scanner.results();
    if (scanSelection < int(found.size())) {
        auto c = found[scanSelection].client;
        choose(c.name + " · " + c.host, {0, 1, 2}, {"Connect", "Save / edit client", "Cancel"}, 0,
               [this, c](auto v) {
                   if (v == 0)
                       beginConnect(c);
                   if (v == 1) {
                       scanOpen = false;
                       page = 4;
                       int index = -1;
                       for (int i = 0; i < int(settings.remoteClients.size()); ++i)
                           if (settings.remoteClients[i].host == c.host) {
                               index = i;
                               break;
                           }
                       beginClientEdit(index);
                       editClient = c;
                   }
               });
    } else if (scanSelection == int(found.size())) {
        if (!scanner.running())
            scanner.start(localSubnetHosts());
    } else {
        scanner.cancel();
        scanOpen = false;
    }
}
void updateConnections() {
    if (probeTask.valid() &&
        probeTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto result = probeTask.get();
        if (connectionOpen) {
            if (!result.online) {
                connectionStage = 1;
                connectionSelection = 0;
            } else {
                pendingClient = result.client;
                canScreen = result.screen;
                if (pendingClient.type == "mister")
                    activateRemote(false);
                else {
                    connectionStage = 2;
                    connectionSelection = 0;
                }
            }
        }
    }
}
void drawConnection() {
    ui.begin();
    header();
    ui.text(80, 125, pendingClient.name.empty() ? pendingClient.host : pendingClient.name, 28,
            ACCENT, 1100);
    ui.text(80, 174, pendingClient.host + ":8182", 20, MUTED);
    if (connectionStage == 0) {
        ui.text(80, 240, "Checking remote client...", 26, TEXT);
        ui.button(80, 330, 1120, 54, "Back", true);
    }
    if (connectionStage == 1) {
        ui.text(80, 240, "Remote client is offline", 30, TEXT);
        ui.text(80, 292, "Check that Hi-Fi and its web remote are running.", 20, MUTED, 1100);
        ui.button(80, 360, 1120, 54, "Retry", connectionSelection == 0);
        ui.button(80, 430, 1120, 54, "Back", connectionSelection == 1);
    }
    if (connectionStage == 2) {
        ui.text(80, 235, "Choose remote mode", 28, TEXT);
        ui.button(80, 310, 1120, 58, "Independent control", connectionSelection == 0);
        ui.text(98, 375, "Browse and control music separately from the target's screen.", 19, MUTED,
                1050);
        ui.button(80, 420, 1120, 58,
                  canScreen ? "Screen control" : "Screen control · requires NX Hi-Fi v0.9.0",
                  connectionSelection == 1);
        ui.text(98, 485, "View and operate the target's interface.", 19, MUTED, 1050);
        ui.button(80, 535, 1120, 54, "Cancel", connectionSelection == 2);
    }
    ui.text(80, layout().bottom(665), "Up/Down Select   A Confirm   B Back", 17, MUTED, 1100);
    finishFrame();
}
void drawScan() {
    ui.begin();
    header();
    auto found = scanner.results();
    ui.text(46, 110, "SCAN FOR REMOTE CLIENTS", 25, ACCENT);
    auto message = scanner.running() ? "Scanning " + std::to_string(scanner.completed()) + " / " +
                                           std::to_string(scanner.total())
                   : found.empty()   ? "No clients found. Keep Hi-Fi and its web remote open."
                                     : std::to_string(found.size()) + " clients found";
    ui.text(46, 155, message, 18, MUTED, 1180);
    int first = std::max(0, scanSelection - layout().rows() + 1);
    for (int row = 0; row < layout().rows(); ++row) {
        int index = first + row;
        if (index >= int(found.size()))
            break;
        auto &c = found[index].client;
        bool saved = false;
        for (auto &s : settings.remoteClients)
            if (s.host == c.host)
                saved = true;
        ui.button(46, 198 + row * 44, 1180, 40,
                  c.name + " · " + c.host + (c.type == "nx" ? " · NX" : " · MiSTer") +
                      (saved ? " · Saved" : ""),
                  scanSelection == index);
    }
    ui.button(46, layout().bottom(572), 570, 48, scanner.running() ? "Scanning..." : "Scan again",
              scanSelection == int(found.size()));
    ui.button(636, layout().bottom(572), 590, 48, "Cancel / Back",
              scanSelection == int(found.size()) + 1);
    ui.text(46, layout().bottom(660), "A Connect / Save   B Back   Up/Down Select   Port 8182", 17,
            MUTED, 1170);
    drawOptions();
    finishFrame();
}
bool dialogInput(u64 down, u64 held, bool injected) {
    if (!connectionOpen && !scanOpen)
        return false;
    bool up = held & (HidNpadButton_Up | HidNpadButton_StickLUp),
         dn = held & (HidNpadButton_Down | HidNpadButton_StickLDown);
    int delta = 0;
    if (up || dn) {
        if (!repeatFrames || (repeatFrames > 20 && repeatFrames % 6 == 0))
            delta = up ? -1 : 1;
        ++repeatFrames;
    } else
        repeatFrames = 0;
    if (optionApply) {
        if (delta)
            move(delta);
        if (down & HidNpadButton_A)
            action();
        if (down & HidNpadButton_B)
            optionApply = nullptr;
    } else if (connectionOpen) {
        int count = connectionStage == 0 ? 1 : connectionStage == 1 ? 2 : 3;
        connectionSelection = (connectionSelection + delta + count) % count;
        if (down & HidNpadButton_A)
            connectionAction();
        if (down & HidNpadButton_B)
            closeConnection();
    } else {
        int count = scanner.results().size() + 2;
        scanSelection = (scanSelection + delta + count) % count;
        if (down & HidNpadButton_A)
            scanAction();
        if (down & HidNpadButton_B) {
            scanner.cancel();
            scanOpen = false;
        }
    }
    if (!injected) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT)
                running = false;
            if (e.type == SDL_FINGERUP)
                dialogClick(e.tfinger.x * 1280, e.tfinger.y * layout().height());
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.which != SDL_TOUCH_MOUSEID)
                dialogClick(ui.pointerX(e.button.x), e.button.y);
        }
    }
    return true;
}
void dialogClick(int x, int y) {
    if (optionApply) {
        int first = layout().optionFirst(optionIndex);
        if (inside(x, y, 170, 170, 940, layout().optionRows() * 48)) {
            int i = first + (y - 170) / 48;
            if (i < int(optionValues.size())) {
                optionIndex = i;
                action();
            }
        }
        return;
    }
    if (connectionOpen) {
        if (connectionStage == 0 && inside(x, y, 80, 330, 1120, 54))
            closeConnection();
        if (connectionStage == 1)
            for (int i = 0; i < 2; ++i)
                if (inside(x, y, 80, 360 + i * 70, 1120, 54)) {
                    connectionSelection = i;
                    connectionAction();
                    return;
                }
        if (connectionStage == 2) {
            int ys[] = {310, 420, 535};
            for (int i = 0; i < 3; ++i)
                if (inside(x, y, 80, ys[i], 1120, 58)) {
                    connectionSelection = i;
                    connectionAction();
                    return;
                }
        }
    } else if (scanOpen) {
        int first = std::max(0, scanSelection - layout().rows() + 1), n = scanner.results().size();
        if (inside(x, y, 46, 198, 1180, layout().rows() * 44)) {
            int i = first + (y - 198) / 44;
            if (i < n) {
                scanSelection = i;
                scanAction();
                return;
            }
        }
        if (inside(x, y, 46, layout().bottom(572), 570, 48)) {
            scanSelection = n;
            scanAction();
        }
        if (inside(x, y, 636, layout().bottom(572), 590, 48)) {
            scanSelection = n + 1;
            scanAction();
        }
    }
}
u64 screenButtons(unsigned b) {
    const u64 keys[] = {HidNpadButton_A,    HidNpadButton_B,     HidNpadButton_X,
                        HidNpadButton_Y,    HidNpadButton_Minus, HidNpadButton_Plus,
                        HidNpadButton_L,    HidNpadButton_R,     HidNpadButton_ZL,
                        HidNpadButton_ZR,   HidNpadButton_Up,    HidNpadButton_Down,
                        HidNpadButton_Left, HidNpadButton_Right, HidNpadButton_StickR};
    u64 out = 0;
    for (int i = 0; i < 15; ++i)
        if (b & (1u << i))
            out |= keys[i];
    return out;
}
void dispatchScreenInput(const nlohmann::json &j) {
    auto kind = j.value("kind", std::string("buttons"));
    if (kind == "cancel") {
        screenPointerActive = false;
        scrubbing = false;
        colourDrag = -1;
        return;
    }
    bool wasSleeping = sleeping;
    wake();
    if (wasSleeping)
        return;
    if (kind == "buttons") {
        unsigned b = j.value("buttons", 0u) & 32767;
        PadState dummy{};
        cooldown = 0;
        input(dummy, screenButtons(b), screenButtons(b), true);
        return;
    }
    int w = j.value("w", 0), h = j.value("h", 0);
    if (w != layout().width() || h != layout().height())
        return;
    int x = layout().designX(j.value("x", 0)), y = j.value("y", 0);
    if (x < 0 || x > 1280 || y < 0 || y > layout().height())
        return;
    if (targetView && targetView->screenMode())
        return;
    if (targetView) {
        if (kind == "down") {
            screenPointerActive = targetView->beginScrub(x, y);
            screenPointerY = y;
        } else if (kind == "move" && screenPointerActive)
            targetView->previewScrub(x);
        else if (kind == "up") {
            if (screenPointerActive)
                targetView->drag(x);
            else
                targetView->click(x, y);
            screenPointerActive = false;
        }
        return;
    }
    if (connectionOpen || scanOpen) {
        if (kind == "up")
            dialogClick(x, y);
        return;
    }
    if (kind == "down") {
        screenPointerActive = true;
        screenPointerGesture = false;
        screenPointerY = y;
        if (beginColourDrag(x, y))
            screenPointerGesture = true;
        else if (beginScrub(x, y))
            screenPointerGesture = true;
    }
    if (kind == "move" && screenPointerActive) {
        if (colourDrag >= 0)
            dragColour(x);
        else if (scrubbing)
            previewScrub(x);
        else if (!fullscreen && !colourPicking && x < 856 && std::abs(y - screenPointerY) > 32) {
            move(y < screenPointerY ? 1 : -1);
            screenPointerY = y;
            screenPointerGesture = true;
        }
    }
    if (kind == "up" && screenPointerActive) {
        if (colourDrag >= 0) {
            dragColour(x);
            colourDrag = -1;
        } else if (scrubbing)
            finishScrub(x);
        else if (!screenPointerGesture)
            click(x, y);
        screenPointerActive = false;
    }
}
nlohmann::json screenCommand(const nlohmann::json &j) {
    using J = nlohmann::json;
    auto fail = [](std::string e) { return J{{"ok", false}, {"error", e}}; };
    auto op = j.value("op", std::string{});
    auto now = SDL_GetTicks64();
    if (op == "screen_open") {
        if (targetView && targetView->screenMode())
            return fail("This client is already controlling another screen.");
        auto token = screenSessions.open(now);
        if (token.empty())
            return fail("Too many screen controllers.");
        return {{"ok", true}, {"session", token}};
    }
    auto token = j.value("session", std::string{});
    if (!screenSessions.touch(token, now))
        return fail("Screen session expired. Reconnect to the client.");
    if (op == "screen_close") {
        screenSessions.close(token);
        scrubbing = false;
        colourDrag = -1;
        screenPointerActive = false;
        if (remoteKeyboard.owner == token)
            remoteKeyboard.cancel();
        return {{"ok", true}};
    }
    if (op == "screen_keepalive")
        return {{"ok", true}};
    if (op == "screen_text") {
        if (!remoteKeyboard.pending || remoteKeyboard.owner != token ||
            j.value("id", uint64_t(0)) != remoteKeyboard.id)
            return fail("The text prompt is no longer open.");
        auto text = j.value("value", std::string{});
        if (text.size() > 1024 || text.find('\0') != std::string::npos)
            return fail("Invalid text.");
        bool accepted = j.value("accepted", false);
        remoteKeyboard.pending = false;
        if (accepted) {
            if (keyboardReplay.value("kind", std::string{}) == "up") {
                screenPointerActive = true;
                screenPointerGesture = false;
            }
            remoteKeyboard.answer = text;
            remoteKeyboard.accepted = true;
            remoteKeyboard.answerReady = true;
            remoteKeyboard.intercept = true;
            dispatchScreenInput(keyboardReplay);
            remoteKeyboard.intercept = false;
            remoteKeyboard.answerReady = false;
        }
        return {{"ok", true}};
    }
    if (op != "screen_input")
        return fail("Unknown screen command.");
    remoteKeyboard.cancel();
    remoteKeyboard.owner = token;
    remoteKeyboard.intercept = true;
    dispatchScreenInput(j);
    remoteKeyboard.intercept = false;
    if (remoteKeyboard.pending)
        keyboardReplay = j;
    return {{"ok", true}};
}
void finishFrame(bool present = true) {
    auto now = SDL_GetTicks64();
    if (remote && ui.recordingScreen() && screenSessions.active(now) &&
        now - lastScreenPublish >= 50) {
        lastScreenPublish = now;
        auto frame = ui.screenFrame();
        if (remoteKeyboard.pending)
            frame["keyboard"] = {{"id", remoteKeyboard.id},
                                 {"owner", remoteKeyboard.owner},
                                 {"label", remoteKeyboard.label},
                                 {"value", remoteKeyboard.initial},
                                 {"secret", remoteKeyboard.secret}};
        remote->screenFrame(std::move(frame), ui.screenCover());
    }
    if (present)
        SDL_RenderPresent(renderer);
}
