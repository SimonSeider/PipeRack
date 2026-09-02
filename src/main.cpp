#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QPalette>
#include <QSocketNotifier>
#include <QStyleFactory>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include "MainWindow.h"
#ifdef HAVE_PIPEWIRE
#include "PipeWireBackend.h"
#endif
#include "Theme.h"
#ifdef HAVE_PULSE
#include "PulseBackend.h"
#endif

static void applyDarkPalette(QApplication &app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    pal.setColor(QPalette::Window, Theme::windowBg());
    pal.setColor(QPalette::WindowText, Theme::textBright());
    pal.setColor(QPalette::Base, Theme::field());
    pal.setColor(QPalette::AlternateBase, Theme::surface());
    pal.setColor(QPalette::ToolTipBase, Theme::surfaceHi());
    pal.setColor(QPalette::ToolTipText, Theme::textBright());
    pal.setColor(QPalette::Text, Theme::textBright());
    pal.setColor(QPalette::Button, Theme::surfaceHi());
    pal.setColor(QPalette::ButtonText, Theme::textBright());
    pal.setColor(QPalette::Highlight, Theme::accent());
    pal.setColor(QPalette::HighlightedText, Theme::windowBg());
    pal.setColor(QPalette::PlaceholderText, Theme::textFaint());
    pal.setColor(QPalette::Disabled, QPalette::WindowText, Theme::textFaint());
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, Theme::textFaint());
    app.setPalette(pal);

    app.setStyleSheet(QStringLiteral(
        "QToolTip { background: #1e222a; color: #e8eaef; border: 1px solid #272c35;"
        " border-radius: 6px; padding: 6px 8px; }"));
}

static int sigFd[2] = {-1, -1};

static void forwardSignal(int)
{
    const char byte = 1;
    const ssize_t written = ::write(sigFd[0], &byte, 1);
    (void)written;
}

static void installSignalHandling(QApplication &app)
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd) != 0)
        return;

    auto *notifier = new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, &app, [notifier]
                     {
        notifier->setEnabled(false);
        char byte = 0;
        const ssize_t got = ::read(sigFd[1], &byte, 1);
        (void)got;
        QCoreApplication::quit(); });

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = forwardSignal;
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGHUP, &sa, nullptr);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("PipeRack"));
    QApplication::setOrganizationName(QStringLiteral("PipeRack"));
    QApplication::setApplicationVersion(QStringLiteral("1.0"));
    QApplication::setDesktopFileName(QStringLiteral("piperack"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Virtual audio cables for Linux, using PipeWire or PulseAudio."));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption backendOpt(
        QStringList{QStringLiteral("b"), QStringLiteral("backend")},
        QStringLiteral("Audio backend to use: auto, pipewire or pulse."),
        QStringLiteral("name"), QStringLiteral("auto"));
    parser.addOption(backendOpt);
    parser.process(app);

    applyDarkPalette(app);

    const QString want = parser.value(backendOpt).toLower();
    AudioBackend *backend = nullptr;
    QStringList tried;

    auto tryStart = [&](AudioBackend *candidate)
    {
        QString err;
        if (candidate->start(&err))
        {
            backend = candidate;
            return true;
        }
        tried.append(QStringLiteral("%1: %2").arg(candidate->displayName(), err));
        delete candidate;
        return false;
    };

    // Only start the first backend that connects; no two servers are started at once.
#ifdef HAVE_PIPEWIRE
    if (!backend && (want == QLatin1String("auto") || want == QLatin1String("pipewire")))
        tryStart(new PipeWireBackend);
#endif
#ifdef HAVE_PULSE
    if (!backend && (want == QLatin1String("auto") || want == QLatin1String("pulse")))
        tryStart(new PulseBackend);
#endif

    if (!backend)
    {
        const QString error =
            tried.isEmpty()
                ? QStringLiteral("This build has no backend for “%1”. It was compiled with: %2.")
                      .arg(want, QStringLiteral(
#if defined(HAVE_PIPEWIRE) && defined(HAVE_PULSE)
                                      "pipewire, pulse"
#elif defined(HAVE_PIPEWIRE)
                                      "pipewire"
#elif defined(HAVE_PULSE)
                                      "pulse"
#else
                                      "nothing"
#endif
                                      ))
                : tried.join(QStringLiteral("\n"));
        QMessageBox::critical(nullptr, QStringLiteral("PipeRack cannot start"), QStringLiteral("PipeRack could not connect to an audio " "server.\n\n%1").arg(error));
        return 1;
    }

    installSignalHandling(app);

    MainWindow win(backend);
    win.show();
    win.restoreSession();

    const int rc = app.exec();
    backend->stop();
    delete backend;
    return rc;
}
