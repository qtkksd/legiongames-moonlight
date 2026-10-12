#include "streamingpreferences.h"
#include "utils.h"
#include "SDL_compat.h"

#include <QSettings>
#include <QTranslator>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QLocale>
#include <QReadWriteLock>
#include <QtMath>

#include <QtDebug>

#define SER_STREAMSETTINGS "streamsettings"
#define SER_WIDTH "width"
#define SER_HEIGHT "height"
#define SER_FPS "fps"
#define SER_BITRATE "bitrate"
#define SER_UNLOCK_BITRATE "unlockbitrate"
#define SER_AUTOADJUSTBITRATE "autoadjustbitrate"
#define SER_FULLSCREEN "fullscreen"
#define SER_VSYNC "vsync"
#define SER_GAMEOPTS "gameopts"
#define SER_HOSTAUDIO "hostaudio"
#define SER_MULTICONT "multicontroller"
#define SER_AUDIOCFG "audiocfg"
#define SER_VIDEOCFG "videocfg"
#define SER_HDR "hdr"
#define SER_YUV444 "yuv444"
#define SER_VIDEODEC "videodec"
#define SER_WINDOWMODE "windowmode"
#define SER_MDNS "mdns"
#define SER_QUITAPPAFTER "quitAppAfter"
#define SER_ABSMOUSEMODE "mouseacceleration"
#define SER_ABSTOUCHMODE "abstouchmode"
#define SER_STARTWINDOWED "startwindowed"
#define SER_FRAMEPACING "framepacing"
#define SER_CONNWARNINGS "connwarnings"
#define SER_CONFWARNINGS "confwarnings"
#define SER_UIDISPLAYMODE "uidisplaymode"
#define SER_RICHPRESENCE "richpresence"
#define SER_GAMEPADMOUSE "gamepadmouse"
#define SER_DEFAULTVER "defaultver"
#define SER_PACKETSIZE "packetsize"
#define SER_DETECTNETBLOCKING "detectnetblocking"
#define SER_SHOWPERFOVERLAY "showperfoverlay"
#define SER_SWAPMOUSEBUTTONS "swapmousebuttons"
#define SER_MUTEONFOCUSLOSS "muteonfocusloss"
#define SER_BACKGROUNDGAMEPAD "backgroundgamepad"
#define SER_REVERSESCROLL "reversescroll"
#define SER_SWAPFACEBUTTONS "swapfacebuttons"
#define SER_CAPTURESYSKEYS "capturesyskeys"
#define SER_KEEPAWAKE "keepawake"
#define SER_LANGUAGE "language"
#define SER_STREAMMIC "streammic"
#define SER_MIC_CAPTURE_DEVICE "miccapturedevice"
#define SER_SHORTCUTS "shortcuts"

#define CURRENT_DEFAULT_VER 3

static StreamingPreferences* s_GlobalPrefs;

// Best-effort detection of the client's native display resolution and refresh
// rate, used as the default stream settings on a fresh install.
static void getNativeDisplayDefaults(int& w, int& h, int& f)
{
    w = 1280;
    h = 720;
    f = 60;

    QGuiApplication* guiApp = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    if (guiApp == nullptr) {
        return;
    }

    QScreen* screen = guiApp->primaryScreen();
    if (screen == nullptr) {
        return;
    }

    const QSize size = screen->size();
    if (size.isValid() && size.width() > 0 && size.height() > 0) {
        w = size.width();
        h = size.height();
    }

    const int refreshRate = qRound(screen->refreshRate());
    if (refreshRate >= 24 && refreshRate <= 480) {
        f = refreshRate;
    }
}

Q_GLOBAL_STATIC(QReadWriteLock, s_GlobalPrefsLock)

StreamingPreferences::StreamingPreferences(QQmlEngine *qmlEngine)
    : m_QmlEngine(qmlEngine)
{
    reload();
}

StreamingPreferences* StreamingPreferences::get(QQmlEngine *qmlEngine)
{
    {
        QReadLocker readGuard(s_GlobalPrefsLock);

        // If we have a preference object and it's associated with a QML engine or
        // if the caller didn't specify a QML engine, return the existing object.
        if (s_GlobalPrefs && (s_GlobalPrefs->m_QmlEngine || !qmlEngine)) {
            // The lifetime logic here relies on the QML engine also being a singleton.
            Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            return s_GlobalPrefs;
        }
    }

    {
        QWriteLocker writeGuard(s_GlobalPrefsLock);

        // If we already have an preference object but the QML engine is now available,
        // associate the QML engine with the preferences.
        if (s_GlobalPrefs) {
            if (!s_GlobalPrefs->m_QmlEngine) {
                s_GlobalPrefs->m_QmlEngine = qmlEngine;
            }
            else {
                // We could reach this codepath if another thread raced with us
                // and created the object while we were outside the pref lock.
                Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            }
        }
        else {
            s_GlobalPrefs = new StreamingPreferences(qmlEngine);
        }

        return s_GlobalPrefs;
    }
}

void StreamingPreferences::reload()
{
    QSettings settings;

    int defaultVer = settings.value(SER_DEFAULTVER, 0).toInt();

#ifdef Q_OS_DARWIN
    recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
#else
    // Wayland doesn't support modesetting, so use fullscreen desktop mode
    // unless we have a slow GPU (which can take advantage of wp_viewporter
    // to reduce GPU load with lower resolution video streams).
    if (WMUtils::isRunningWayland() && !WMUtils::isGpuSlow()) {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
    }
    else {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN;
    }
#endif

    int nativeWidth, nativeHeight, nativeFps;
    getNativeDisplayDefaults(nativeWidth, nativeHeight, nativeFps);
    width = settings.value(SER_WIDTH, nativeWidth).toInt();
    height = settings.value(SER_HEIGHT, nativeHeight).toInt();
    fps = settings.value(SER_FPS, nativeFps).toInt();
    enableYUV444 = settings.value(SER_YUV444, false).toBool();
    bitrateKbps = settings.value(SER_BITRATE, getDefaultBitrate(width, height, fps, enableYUV444)).toInt();
    unlockBitrate = settings.value(SER_UNLOCK_BITRATE, false).toBool();
    autoAdjustBitrate = settings.value(SER_AUTOADJUSTBITRATE, true).toBool();
    enableVsync = settings.value(SER_VSYNC, true).toBool();
    gameOptimizations = settings.value(SER_GAMEOPTS, true).toBool();
    playAudioOnHost = settings.value(SER_HOSTAUDIO, false).toBool();
    multiController = settings.value(SER_MULTICONT, true).toBool();
    enableMdns = settings.value(SER_MDNS, true).toBool();
    quitAppAfter = settings.value(SER_QUITAPPAFTER, false).toBool();
    absoluteMouseMode = settings.value(SER_ABSMOUSEMODE, false).toBool();
    absoluteTouchMode = settings.value(SER_ABSTOUCHMODE, true).toBool();
    framePacing = settings.value(SER_FRAMEPACING, false).toBool();
    connectionWarnings = settings.value(SER_CONNWARNINGS, true).toBool();
    configurationWarnings = settings.value(SER_CONFWARNINGS, true).toBool();
    richPresence = settings.value(SER_RICHPRESENCE, false).toBool();
    gamepadMouse = settings.value(SER_GAMEPADMOUSE, true).toBool();
    detectNetworkBlocking = settings.value(SER_DETECTNETBLOCKING, true).toBool();
    showPerformanceOverlay = settings.value(SER_SHOWPERFOVERLAY, false).toBool();
    packetSize = settings.value(SER_PACKETSIZE, 0).toInt();
    swapMouseButtons = settings.value(SER_SWAPMOUSEBUTTONS, false).toBool();
    muteOnFocusLoss = settings.value(SER_MUTEONFOCUSLOSS, false).toBool();
    backgroundGamepad = settings.value(SER_BACKGROUNDGAMEPAD, false).toBool();
    reverseScrollDirection = settings.value(SER_REVERSESCROLL, false).toBool();
    swapFaceButtons = settings.value(SER_SWAPFACEBUTTONS, false).toBool();
    keepAwake = settings.value(SER_KEEPAWAKE, true).toBool();
    streamMicToHost = settings.value(SER_STREAMMIC, true).toBool();
    micCaptureDevice = settings.value(SER_MIC_CAPTURE_DEVICE, QString()).toString();
    enableHdr = settings.value(SER_HDR, false).toBool();
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(settings.value(SER_CAPTURESYSKEYS,
                                                         static_cast<int>(CaptureSysKeysMode::CSK_OFF)).toInt());
    audioConfig = static_cast<AudioConfig>(settings.value(SER_AUDIOCFG,
                                                  static_cast<int>(AudioConfig::AC_STEREO)).toInt());
    videoCodecConfig = static_cast<VideoCodecConfig>(settings.value(SER_VIDEOCFG,
                                                  static_cast<int>(VideoCodecConfig::VCC_AUTO)).toInt());
    videoDecoderSelection = static_cast<VideoDecoderSelection>(settings.value(SER_VIDEODEC,
                                                  static_cast<int>(VideoDecoderSelection::VDS_AUTO)).toInt());
    windowMode = static_cast<WindowMode>(settings.value(SER_WINDOWMODE,
                                                        // Try to load from the old preference value too
                                                        static_cast<int>(settings.value(SER_FULLSCREEN, true).toBool() ?
                                                                             recommendedFullScreenMode : WindowMode::WM_WINDOWED)).toInt());
    uiDisplayMode = static_cast<UIDisplayMode>(settings.value(SER_UIDISPLAYMODE,
                                               static_cast<int>(settings.value(SER_STARTWINDOWED, true).toBool() ? UIDisplayMode::UI_WINDOWED
                                                                                                                 : UIDisplayMode::UI_MAXIMIZED)).toInt());
    language = static_cast<Language>(settings.value(SER_LANGUAGE,
                                                    static_cast<int>(Language::LANG_RU)).toInt());

    // Load user-configurable in-stream shortcuts (numeric SDL records). Any
    // missing/invalid entry falls back to the built-in default so the defaults
    // always remain usable as backups.
    m_Shortcuts = settings.value(SER_SHORTCUTS).toMap();
    {
        const QVariantMap defaults = defaultShortcuts();
        for (auto it = defaults.constBegin(); it != defaults.constEnd(); ++it) {
            const QVariantMap rec = m_Shortcuts.value(it.key()).toMap();
            if (!rec.contains(QStringLiteral("sc")) || !rec.contains(QStringLiteral("mod")) ||
                    rec.value(QStringLiteral("mod")).toInt() == 0) {
                m_Shortcuts[it.key()] = it.value();
            }
        }
    }


    // Perform default settings updates as required based on last default version
    if (defaultVer < 1) {
#ifdef Q_OS_DARWIN
        // Update window mode setting on macOS from full-screen (old default) to borderless windowed (new default)
        if (windowMode == WindowMode::WM_FULLSCREEN) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
#endif
    }
    if (defaultVer < 2) {
        if (windowMode == WindowMode::WM_FULLSCREEN && WMUtils::isRunningWayland()) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
    }
    if (defaultVer < 3) {
        // LegionGames branding defaults
        language = Language::LANG_RU;
        richPresence = false;
    }

    // Fixup VCC value to the new settings format with codec and HDR separate
    if (videoCodecConfig == VCC_FORCE_HEVC_HDR_DEPRECATED) {
        videoCodecConfig = VCC_AUTO;
        enableHdr = true;
    }
}

bool StreamingPreferences::retranslate()
{
    static QTranslator* translator = nullptr;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    if (m_QmlEngine != nullptr) {
        // Dynamic retranslation is not supported until Qt 5.10
        return false;
    }
#endif

    QTranslator* newTranslator = new QTranslator();
    QString languageSuffix = getSuffixFromLanguage(language);

    // Remove the old translator, even if we can't load a new one.
    // Otherwise we'll be stuck with the old translated values instead
    // of defaulting to English.
    if (translator != nullptr) {
        QCoreApplication::removeTranslator(translator);
        delete translator;
        translator = nullptr;
    }

    if (newTranslator->load(QString(":/languages/qml_") + languageSuffix)) {
        qInfo() << "Successfully loaded translation for" << languageSuffix;

        translator = newTranslator;
        QCoreApplication::installTranslator(translator);
    }
    else {
        qInfo() << "No translation available for" << languageSuffix;
        delete newTranslator;
    }

    if (m_QmlEngine != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        // This is a dynamic retranslation from the settings page.
        // We have to kick the QML engine into reloading our text.
        m_QmlEngine->retranslate();
#else
        // Unreachable below Qt 5.10 due to the check above
        Q_ASSERT(false);
#endif
    }
    else {
        // This is a translation from a non-QML context, which means
        // it is probably app startup. There's nothing to refresh.
    }

    return true;
}

QStringList StreamingPreferences::getMicCaptureDeviceNames() const
{
    QStringList devices;

    const bool audioWasAlreadyInitialized = (SDL_WasInit(SDL_INIT_AUDIO) != 0);
    if (!audioWasAlreadyInitialized && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        qWarning() << "Failed to initialize SDL audio while enumerating microphone devices:"
                   << SDL_GetError();
        return devices;
    }

    int count = SDL_GetNumAudioDevices(/*iscapture=*/1);
    if (count < 0) {
        qWarning() << "Failed to enumerate microphone devices:" << SDL_GetError();
    }
    else {
        for (int i = 0; i < count; i++) {
            const char* name = SDL_GetAudioDeviceName(i, /*iscapture=*/1);
            if (name != nullptr && name[0] != '\0') {
                devices.append(QString::fromUtf8(name));
            }
        }
    }

    if (!audioWasAlreadyInitialized) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    devices.removeDuplicates();
    return devices;
}

QString StreamingPreferences::getSuffixFromLanguage(StreamingPreferences::Language lang)
{
    switch (lang)
    {
    case LANG_DE:
        return "de";
    case LANG_EN:
        return "en";
    case LANG_FR:
        return "fr";
    case LANG_ZH_CN:
        return "zh_CN";
    case LANG_NB_NO:
        return "nb_NO";
    case LANG_RU:
        return "ru";
    case LANG_ES:
        return "es";
    case LANG_JA:
        return "ja";
    case LANG_VI:
        return "vi";
    case LANG_TH:
        return "th";
    case LANG_KO:
        return "ko";
    case LANG_HU:
        return "hu";
    case LANG_NL:
        return "nl";
    case LANG_SV:
        return "sv";
    case LANG_TR:
        return "tr";
    case LANG_UK:
        return "uk";
    case LANG_ZH_TW:
        return "zh_TW";
    case LANG_PT:
        return "pt";
    case LANG_PT_BR:
        return "pt_BR";
    case LANG_EL:
        return "el";
    case LANG_IT:
        return "it";
    case LANG_HI:
        return "hi";
    case LANG_PL:
        return "pl";
    case LANG_CS:
        return "cs";
    case LANG_HE:
        return "he";
    case LANG_CKB:
        return "ckb";
    case LANG_LT:
        return "lt";
    case LANG_ET:
        return "et";
    case LANG_BG:
        return "bg";
    case LANG_EO:
        return "eo";
    case LANG_TA:
        return "ta";
    case LANG_AUTO:
    default:
        return QLocale::system().name();
    }
}

void StreamingPreferences::save()
{
    QSettings settings;

    settings.setValue(SER_WIDTH, width);
    settings.setValue(SER_HEIGHT, height);
    settings.setValue(SER_FPS, fps);
    settings.setValue(SER_BITRATE, bitrateKbps);
    settings.setValue(SER_UNLOCK_BITRATE, unlockBitrate);
    settings.setValue(SER_AUTOADJUSTBITRATE, autoAdjustBitrate);
    settings.setValue(SER_VSYNC, enableVsync);
    settings.setValue(SER_GAMEOPTS, gameOptimizations);
    settings.setValue(SER_HOSTAUDIO, playAudioOnHost);
    settings.setValue(SER_MULTICONT, multiController);
    settings.setValue(SER_MDNS, enableMdns);
    settings.setValue(SER_QUITAPPAFTER, quitAppAfter);
    settings.setValue(SER_ABSMOUSEMODE, absoluteMouseMode);
    settings.setValue(SER_ABSTOUCHMODE, absoluteTouchMode);
    settings.setValue(SER_FRAMEPACING, framePacing);
    settings.setValue(SER_CONNWARNINGS, connectionWarnings);
    settings.setValue(SER_CONFWARNINGS, configurationWarnings);
    settings.setValue(SER_RICHPRESENCE, richPresence);
    settings.setValue(SER_GAMEPADMOUSE, gamepadMouse);
    settings.setValue(SER_PACKETSIZE, packetSize);
    settings.setValue(SER_DETECTNETBLOCKING, detectNetworkBlocking);
    settings.setValue(SER_SHOWPERFOVERLAY, showPerformanceOverlay);
    settings.setValue(SER_AUDIOCFG, static_cast<int>(audioConfig));
    settings.setValue(SER_HDR, enableHdr);
    settings.setValue(SER_YUV444, enableYUV444);
    settings.setValue(SER_VIDEOCFG, static_cast<int>(videoCodecConfig));
    settings.setValue(SER_VIDEODEC, static_cast<int>(videoDecoderSelection));
    settings.setValue(SER_WINDOWMODE, static_cast<int>(windowMode));
    settings.setValue(SER_UIDISPLAYMODE, static_cast<int>(uiDisplayMode));
    settings.setValue(SER_LANGUAGE, static_cast<int>(language));
    settings.setValue(SER_DEFAULTVER, CURRENT_DEFAULT_VER);
    settings.setValue(SER_SWAPMOUSEBUTTONS, swapMouseButtons);
    settings.setValue(SER_MUTEONFOCUSLOSS, muteOnFocusLoss);
    settings.setValue(SER_BACKGROUNDGAMEPAD, backgroundGamepad);
    settings.setValue(SER_REVERSESCROLL, reverseScrollDirection);
    settings.setValue(SER_SWAPFACEBUTTONS, swapFaceButtons);
    settings.setValue(SER_CAPTURESYSKEYS, captureSysKeysMode);
    settings.setValue(SER_KEEPAWAKE, keepAwake);
    settings.setValue(SER_STREAMMIC, streamMicToHost);
    settings.setValue(SER_MIC_CAPTURE_DEVICE, micCaptureDevice);
    settings.setValue(SER_SHORTCUTS, m_Shortcuts);
}

static QString sdlScancodeName(int sc)
{
    switch (sc) {
    case SDL_SCANCODE_SPACE: return QStringLiteral("Space");
    case SDL_SCANCODE_TAB: return QStringLiteral("Tab");
    case SDL_SCANCODE_RETURN: return QStringLiteral("Return");
    case SDL_SCANCODE_BACKSPACE: return QStringLiteral("Backspace");
    case SDL_SCANCODE_DELETE: return QStringLiteral("Delete");
    case SDL_SCANCODE_INSERT: return QStringLiteral("Insert");
    case SDL_SCANCODE_HOME: return QStringLiteral("Home");
    case SDL_SCANCODE_END: return QStringLiteral("End");
    case SDL_SCANCODE_PAGEUP: return QStringLiteral("PageUp");
    case SDL_SCANCODE_PAGEDOWN: return QStringLiteral("PageDown");
    case SDL_SCANCODE_UP: return QStringLiteral("Up");
    case SDL_SCANCODE_DOWN: return QStringLiteral("Down");
    case SDL_SCANCODE_LEFT: return QStringLiteral("Left");
    case SDL_SCANCODE_RIGHT: return QStringLiteral("Right");
    case SDL_SCANCODE_MINUS: return QStringLiteral("-");
    case SDL_SCANCODE_EQUALS: return QStringLiteral("=");
    case SDL_SCANCODE_LEFTBRACKET: return QStringLiteral("[");
    case SDL_SCANCODE_RIGHTBRACKET: return QStringLiteral("]");
    case SDL_SCANCODE_SEMICOLON: return QStringLiteral(";");
    case SDL_SCANCODE_APOSTROPHE: return QStringLiteral("'");
    case SDL_SCANCODE_COMMA: return QStringLiteral(",");
    case SDL_SCANCODE_PERIOD: return QStringLiteral(".");
    case SDL_SCANCODE_SLASH: return QStringLiteral("/");
    case SDL_SCANCODE_BACKSLASH: return QStringLiteral("\\");
    case SDL_SCANCODE_GRAVE: return QStringLiteral("`");
    default: return QString::number(sc);
    }
}

bool StreamingPreferences::qtKeyToSdl(int qtKey, int qtModifiers, int& scanCode, int& keyCode, int& mod)
{
    mod = 0;
    if (qtModifiers & Qt::ControlModifier) mod |= KMOD_CTRL;
    if (qtModifiers & Qt::AltModifier)     mod |= KMOD_ALT;
    if (qtModifiers & Qt::ShiftModifier)   mod |= KMOD_SHIFT;
    if (qtModifiers & Qt::MetaModifier)    mod |= KMOD_GUI;

    // Letters (SDL scancodes A..Z are contiguous; lowercase SDLK keycodes too)
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) {
        scanCode = SDL_SCANCODE_A + (qtKey - Qt::Key_A);
        keyCode  = SDLK_a + (qtKey - Qt::Key_A);
        return true;
    }

    // Digits (SDL defines SDL_SCANCODE_0 after 1..9, handled explicitly)
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) {
        const int d = qtKey - Qt::Key_0; // 0..9
        scanCode = (d == 0) ? SDL_SCANCODE_0 : (SDL_SCANCODE_1 + (d - 1));
        keyCode  = SDLK_0 + d;
        return true;
    }

    // Function keys F1..F12
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F12) {
        scanCode = SDL_SCANCODE_F1 + (qtKey - Qt::Key_F1);
        keyCode  = SDLK_F1 + (qtKey - Qt::Key_F1);
        return true;
    }

    // Special / punctuation keys. We match on scancode at runtime, so the
    // keycode here is only a best-effort secondary match.
    struct { int qt; SDL_Scancode sc; } specials[] = {
        { Qt::Key_Space, SDL_SCANCODE_SPACE },
        { Qt::Key_Tab, SDL_SCANCODE_TAB },
        { Qt::Key_Return, SDL_SCANCODE_RETURN },
        { Qt::Key_Enter, SDL_SCANCODE_RETURN },
        { Qt::Key_Backspace, SDL_SCANCODE_BACKSPACE },
        { Qt::Key_Delete, SDL_SCANCODE_DELETE },
        { Qt::Key_Insert, SDL_SCANCODE_INSERT },
        { Qt::Key_Home, SDL_SCANCODE_HOME },
        { Qt::Key_End, SDL_SCANCODE_END },
        { Qt::Key_PageUp, SDL_SCANCODE_PAGEUP },
        { Qt::Key_PageDown, SDL_SCANCODE_PAGEDOWN },
        { Qt::Key_Up, SDL_SCANCODE_UP },
        { Qt::Key_Down, SDL_SCANCODE_DOWN },
        { Qt::Key_Left, SDL_SCANCODE_LEFT },
        { Qt::Key_Right, SDL_SCANCODE_RIGHT },
        { Qt::Key_Minus, SDL_SCANCODE_MINUS },
        { Qt::Key_Equal, SDL_SCANCODE_EQUALS },
        { Qt::Key_BracketLeft, SDL_SCANCODE_LEFTBRACKET },
        { Qt::Key_BracketRight, SDL_SCANCODE_RIGHTBRACKET },
        { Qt::Key_Semicolon, SDL_SCANCODE_SEMICOLON },
        { Qt::Key_Apostrophe, SDL_SCANCODE_APOSTROPHE },
        { Qt::Key_Comma, SDL_SCANCODE_COMMA },
        { Qt::Key_Period, SDL_SCANCODE_PERIOD },
        { Qt::Key_Slash, SDL_SCANCODE_SLASH },
        { Qt::Key_Backslash, SDL_SCANCODE_BACKSLASH },
        { Qt::Key_QuoteLeft, SDL_SCANCODE_GRAVE },
    };
    for (const auto& s : specials) {
        if (qtKey == s.qt) {
            scanCode = s.sc;
            keyCode = SDL_SCANCODE_TO_KEYCODE(s.sc);
            return true;
        }
    }

    return false;
}

QVariantMap StreamingPreferences::defaultShortcuts()
{
    const int mod = KMOD_CTRL | KMOD_ALT | KMOD_SHIFT;
    auto rec = [mod](SDL_Scancode sc, SDL_Keycode sym) {
        QVariantMap m;
        m[QStringLiteral("sc")] = static_cast<int>(sc);
        m[QStringLiteral("sym")] = static_cast<int>(sym);
        m[QStringLiteral("mod")] = mod;
        return m;
    };

    QVariantMap map;
    map[QStringLiteral("quit")]              = rec(SDL_SCANCODE_Q, SDLK_q);
    map[QStringLiteral("ungrab")]            = rec(SDL_SCANCODE_Z, SDLK_z);
    map[QStringLiteral("toggleFullscreen")]  = rec(SDL_SCANCODE_X, SDLK_x);
    map[QStringLiteral("toggleStats")]       = rec(SDL_SCANCODE_S, SDLK_s);
    map[QStringLiteral("toggleMouseMode")]   = rec(SDL_SCANCODE_M, SDLK_m);
    map[QStringLiteral("toggleCursorHide")]  = rec(SDL_SCANCODE_C, SDLK_c);
    map[QStringLiteral("toggleMinimize")]    = rec(SDL_SCANCODE_D, SDLK_d);
    map[QStringLiteral("pasteText")]         = rec(SDL_SCANCODE_V, SDLK_v);
    map[QStringLiteral("togglePointerLock")] = rec(SDL_SCANCODE_L, SDLK_l);
    map[QStringLiteral("quitAndExit")]       = rec(SDL_SCANCODE_E, SDLK_e);
    return map;
}

QVariantMap StreamingPreferences::shortcutFor(const QString& action) const
{
    QVariantMap rec = m_Shortcuts.value(action).toMap();
    if (!rec.contains(QStringLiteral("sc")) || rec.value(QStringLiteral("mod")).toInt() == 0) {
        // Back up to the built-in default for this action.
        rec = defaultShortcuts().value(action).toMap();
    }
    return rec;
}

QString StreamingPreferences::shortcutText(const QString& action) const
{
    const QVariantMap rec = shortcutFor(action);
    const int mod = rec.value(QStringLiteral("mod")).toInt();
    const int sc = rec.value(QStringLiteral("sc")).toInt();
    const int sym = rec.value(QStringLiteral("sym")).toInt();

    QString s;
    if (mod & KMOD_CTRL)  s += QStringLiteral("Ctrl+");
    if (mod & KMOD_ALT)   s += QStringLiteral("Alt+");
    if (mod & KMOD_SHIFT) s += QStringLiteral("Shift+");
    if (mod & KMOD_GUI)   s += QStringLiteral("Meta+");

    if (sym >= 'a' && sym <= 'z') {
        s += QString(QChar(sym)).toUpper();
    }
    else if (sym >= '0' && sym <= '9') {
        s += QString(QChar(sym));
    }
    else {
        s += sdlScancodeName(sc);
    }
    return s;
}

bool StreamingPreferences::setShortcut(const QString& action, int qtKey, int qtModifiers)
{
    int scanCode = 0, keyCode = 0, mod = 0;
    if (!qtKeyToSdl(qtKey, qtModifiers, scanCode, keyCode, mod)) {
        return false;
    }
    if (mod == 0) {
        // Require at least one modifier so a bare gameplay key can't be hijacked.
        return false;
    }

    QVariantMap rec;
    rec[QStringLiteral("sc")] = scanCode;
    rec[QStringLiteral("sym")] = keyCode;
    rec[QStringLiteral("mod")] = mod;
    m_Shortcuts[action] = rec;
    emit shortcutsChanged();
    return true;
}

void StreamingPreferences::resetShortcut(const QString& action)
{
    m_Shortcuts[action] = defaultShortcuts().value(action).toMap();
    emit shortcutsChanged();
}

int StreamingPreferences::getDefaultBitrate(int width, int height, int fps, bool yuv444)
{
    // Don't scale bitrate linearly beyond 60 FPS. It's definitely not a linear
    // bitrate increase for frame rate once we get to values that high.
    float frameRateFactor = (fps <= 60 ? fps : (qSqrt(fps / 60.f) * 60.f)) / 30.f;

    // TODO: Collect some empirical data to see if these defaults make sense.
    // We're just using the values that the Shield used, as we have for years.
    static const struct resTable {
        int pixels;
        int factor;
    } resTable[] {
        { 640 * 360, 1 },
        { 854 * 480, 2 },
        { 1280 * 720, 5 },
        { 1920 * 1080, 10 },
        { 2560 * 1440, 20 },
        { 3840 * 2160, 40 },
        { -1, -1 },
    };

    // Calculate the resolution factor by linear interpolation of the resolution table
    float resolutionFactor;
    int pixels = width * height;
    for (int i = 0;; i++) {
        if (pixels == resTable[i].pixels) {
            // We can bail immediately for exact matches
            resolutionFactor = resTable[i].factor;
            break;
        }
        else if (pixels < resTable[i].pixels) {
            if (i == 0) {
                // Never go below the lowest resolution entry
                resolutionFactor = resTable[i].factor;
            }
            else {
                // Interpolate between the entry greater than the chosen resolution (i) and the entry less than the chosen resolution (i-1)
                resolutionFactor = ((float)(pixels - resTable[i-1].pixels) / (resTable[i].pixels - resTable[i-1].pixels)) * (resTable[i].factor - resTable[i-1].factor) + resTable[i-1].factor;
            }
            break;
        }
        else if (resTable[i].pixels == -1) {
            // Never go above the highest resolution entry
            resolutionFactor = resTable[i-1].factor;
            break;
        }
    }

    if (yuv444) {
        // This is rough estimation based on the fact that 4:4:4 doubles the amount of raw YUV data compared to 4:2:0
        resolutionFactor *= 2;
    }

    return qRound(resolutionFactor * frameRateFactor) * 1000;
}
