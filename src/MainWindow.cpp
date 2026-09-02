#include "MainWindow.h"
#include "RackUnit.h"
#include "RackWidgets.h"
#include "Theme.h"

#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

static constexpr int kTickMs = 16;

MainWindow::MainWindow(AudioBackend *backend, QWidget *parent)
    : QMainWindow(parent), m_backend(backend)
{
    setWindowTitle(QStringLiteral("PipeRack"));
    setMinimumSize(940, 560);
    resize(1120, 720);

    auto *central = new QWidget(this);
    central->setAutoFillBackground(false);
    auto *outer = new QVBoxLayout(central);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *brow = new QWidget(central);
    brow->setFixedHeight(56);
    auto *browLay = new QHBoxLayout(brow);
    browLay->setContentsMargins(22, 0, 22, 0);
    browLay->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("PipeRack"), brow);
    title->setFont(Theme::labelFont(17, true));
    title->setStyleSheet(QStringLiteral("color: #e8eaef;"));

    m_badge = new QLabel(m_backend->displayName(), brow);
    m_badge->setFont(Theme::labelFont(11));
    m_badge->setStyleSheet(QStringLiteral(
        "color: #98a0ac; border: 1px solid #272c35; border-radius: 9px; padding: 3px 9px;"));

    browLay->addWidget(title);
    browLay->addWidget(m_badge);
    browLay->addStretch(1);
    outer->addWidget(brow);

    auto *seam = new QFrame(central);
    seam->setFixedHeight(1);
    seam->setStyleSheet(QStringLiteral("background: #272c35;"));
    outer->addWidget(seam);

    m_scroll = new QScrollArea(central);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    m_scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #272c35; border-radius: 5px; min-height: 40px; }"
        "QScrollBar::handle:vertical:hover { background: #3a414d; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"));

    m_rackHost = new QWidget;
    m_rackHost->setAutoFillBackground(false);
    m_rackLayout = new QVBoxLayout(m_rackHost);
    m_rackLayout->setContentsMargins(18, 16, 18, 18);
    m_rackLayout->setSpacing(10);

    m_empty = new QLabel(m_rackHost);
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    m_empty->setFont(Theme::labelFont(13));
    m_empty->setStyleSheet(QStringLiteral("color: #636b77; padding: 54px 40px;"));
    m_empty->setText(QStringLiteral(
        "<div style='line-height:150%'>"
        "<span style='font-size:15px; color:#98a0ac;'>The rack is empty.</span><br><br>"
        "Add a cable to create a matched pair of virtual devices.<br>"
        "Send audio to the cable's <b style='color:#5b9dff'>output</b> and any app can record it "
        "from the cable's <b style='color:#3dcf8e'>input</b>.</div>"));

    // The add button sits below every rack, scrolling with them.
    m_addRow = new QWidget(m_rackHost);
    auto *addLay = new QHBoxLayout(m_addRow);
    addLay->setContentsMargins(0, 2, 0, 0);
    addLay->setSpacing(0);

    m_add = new RackButton(QStringLiteral("+  Add Cable"), m_addRow);
    m_add->setGhost(true);
    m_add->setActiveColour(Theme::accent());
    m_add->setFixedHeight(40);
    m_add->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_add, &RackButton::clicked, this, &MainWindow::onAddCable);
    addLay->addWidget(m_add);

    m_rackLayout->addWidget(m_empty);
    m_rackLayout->addWidget(m_addRow);
    m_rackLayout->addStretch(1);

    m_scroll->setWidget(m_rackHost);
    outer->addWidget(m_scroll, 1);

    setCentralWidget(central);

    connect(m_backend, &AudioBackend::topologyChanged, this, &MainWindow::onTopologyChanged);
    connect(m_backend, &AudioBackend::sourcesChanged, this, &MainWindow::onSourcesChanged);
    connect(m_backend, &AudioBackend::backendLost, this, &MainWindow::onBackendLost);

    m_clock = new QElapsedTimer;
    m_clock->start();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &MainWindow::onTick);
    timer->start(kTickMs);
}

MainWindow::~MainWindow()
{
    delete m_clock;
}

QString MainWindow::uniqueId() const
{
    for (;;)
    {
        const QString id = QUuid::createUuid().toString(QUuid::Id128).left(8);
        bool taken = false;
        for (const RackUnit *u : m_units)
            if (u->config().id == id)
                taken = true;
        if (!taken)
            return id;
    }
}

QString MainWindow::uniqueName() const
{
    for (int n = 1;; ++n)
    {
        const QString candidate = QStringLiteral("Cable %1").arg(n);
        bool taken = false;
        for (const RackUnit *u : m_units)
            if (u->config().name.compare(candidate, Qt::CaseInsensitive) == 0)
                taken = true;
        if (!taken)
            return candidate;
    }
}

bool MainWindow::openCable(RackUnit *unit, QString *error)
{
    CableHandle *h = m_backend->createCable(unit->config(), error);
    if (!h)
        return false;
    unit->setHandle(h);
    m_backend->applyGain(h, unit->config().gain, unit->config().muted);

    if (!unit->config().inject.isNull())
    {
        QString injectError;
        if (!m_backend->setInjectedSource(h, unit->config().inject, &injectError))
            unit->setInjectedSource(AudioSource());
    }
    return true;
}

void MainWindow::closeCable(RackUnit *unit)
{
    if (CableHandle *h = unit->handle())
    {
        unit->setHandle(nullptr);
        m_backend->destroyCable(h);
    }
}

RackUnit *MainWindow::addUnit(const CableConfig &cfg, bool announceFailure)
{
    auto *unit = new RackUnit(cfg, m_rackHost);
    connect(unit, &RackUnit::removeRequested, this, &MainWindow::onRemoveRequested);
    connect(unit, &RackUnit::renameRequested, this, &MainWindow::onRenameRequested);
    connect(unit, &RackUnit::gainChanged, this, &MainWindow::onGainChanged);
    connect(unit, &RackUnit::injectRequested, this, &MainWindow::onInjectRequested);
    unit->setAvailableSources(m_backend->availableSources());

    QString error;
    if (!openCable(unit, &error))
    {
        if (announceFailure)
            QMessageBox::critical(this, QStringLiteral("Could not create cable"),
                                  QStringLiteral("%1 refused to create “%2”.\n\n%3")
                                      .arg(m_backend->displayName(), cfg.name, error));
        delete unit;
        return nullptr;
    }

    m_rackLayout->insertWidget(m_rackLayout->indexOf(m_addRow), unit);
    m_units.append(unit);
    renumber();
    updateEmptyState();
    saveSession();
    return unit;
}

void MainWindow::onAddCable()
{
    CableConfig cfg;
    cfg.id = uniqueId();
    cfg.name = uniqueName();
    cfg.channels = 2;
    cfg.gain = 1.0f;
    cfg.muted = false;

    if (RackUnit *unit = addUnit(cfg, true))
    {
        QTimer::singleShot(0, this, [this, unit]
                           { m_scroll->ensureWidgetVisible(unit, 0, 12); });
    }
}

void MainWindow::onRemoveRequested(RackUnit *unit)
{
    const int clients = unit->handle()
                            ? unit->handle()->playbackClients() + unit->handle()->captureClients()
                            : 0;
    if (clients > 0)
    {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Remove cable?"),
            QStringLiteral("“%1” still has %2 connected application%3.\n\n"
                           "Removing it will drop those connections.")
                .arg(unit->config().name)
                .arg(clients)
                .arg(clients == 1 ? QString() : QStringLiteral("s")),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
            return;
    }

    closeCable(unit);
    m_units.removeOne(unit);
    m_rackLayout->removeWidget(unit);
    unit->deleteLater();
    renumber();
    updateEmptyState();
    saveSession();
}

void MainWindow::onRenameRequested(RackUnit *unit, const QString &newName)
{
    const int clients = unit->handle()
                            ? unit->handle()->playbackClients() + unit->handle()->captureClients()
                            : 0;
    if (clients > 0)
    {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Rename cable?"),
            QStringLiteral("Renaming rebuilds the cable, which briefly interrupts the "
                           "%1 application%2 using it right now.\n\nRename anyway?")
                .arg(clients)
                .arg(clients == 1 ? QString() : QStringLiteral("s")),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
        if (answer != QMessageBox::Yes)
        {
            unit->setName(unit->config().name);
            return;
        }
    }

    const CableConfig previous = unit->config();
    closeCable(unit);

    unit->setName(newName);
    QString error;
    if (!openCable(unit, &error))
    {
        QMessageBox::critical(this, QStringLiteral("Rename failed"),
                              QStringLiteral("Could not rebuild the cable as “%1”.\n\n%2")
                                  .arg(newName, error));
        unit->setName(previous.name);
        if (!openCable(unit, &error))
            unit->setHandle(nullptr);
    }
    saveSession();
}

void MainWindow::onGainChanged(RackUnit *unit, float linear, bool muted)
{
    if (CableHandle *h = unit->handle())
        m_backend->applyGain(h, linear, muted);
    saveSession();
}

void MainWindow::onInjectRequested(RackUnit *unit, const AudioSource &src)
{
    CableHandle *h = unit->handle();
    if (!h)
    {
        unit->setInjectedSource(unit->config().inject);
        return;
    }

    QString error;
    if (!m_backend->setInjectedSource(h, src, &error))
    {
        QMessageBox::warning(this, QStringLiteral("Could not inject that input"),
                             QStringLiteral("“%1” could not be patched into “%2”.\n\n%3")
                                 .arg(src.description, unit->config().name, error));
        unit->setInjectedSource(unit->config().inject);
        return;
    }

    unit->setInjectedSource(src);
    saveSession();
}

void MainWindow::onSourcesChanged()
{
    const QList<AudioSource> sources = m_backend->availableSources();
    for (RackUnit *u : m_units)
        u->setAvailableSources(sources);
}

void MainWindow::renumber()
{
    for (int i = 0; i < m_units.size(); ++i)
        m_units[i]->setSlotNumber(i + 1);
}

void MainWindow::updateEmptyState()
{
    m_empty->setVisible(m_units.isEmpty());
}

void MainWindow::onTopologyChanged()
{
    for (RackUnit *u : m_units)
        u->refreshFromBackend();
}

void MainWindow::onBackendLost(const QString &reason)
{
    if (m_lost)
        return;
    m_lost = true;

    for (RackUnit *u : m_units)
        u->setHandle(nullptr);

    m_badge->setText(QStringLiteral("%1 · disconnected").arg(m_backend->displayName()));
    m_badge->setStyleSheet(QStringLiteral(
        "color: #ef5a52; border: 1px solid #5a2a27; border-radius: 9px; padding: 3px 9px;"));
    m_add->setEnabled(false);

    QMessageBox::critical(this, QStringLiteral("Audio server connection lost"),
                          QStringLiteral("PipeRack lost its connection to %1 and the virtual "
                                         "cables are gone.\n\n%2\n\nRestart PipeRack once the "
                                         "audio server is running again.")
                              .arg(m_backend->displayName(), reason));
}

void MainWindow::onTick()
{
    const qint64 ns = m_clock->nsecsElapsed();
    m_clock->restart();
    qreal dt = qreal(ns) / 1e9;
    if (dt > 0.25)
        dt = 0.25;

    for (RackUnit *u : m_units)
        u->tick(dt);
}

void MainWindow::saveSession() const
{
    if (m_restoring)
        return;

    QSettings s;
    s.beginWriteArray(QStringLiteral("cables"), m_units.size());
    for (int i = 0; i < m_units.size(); ++i)
    {
        const CableConfig &c = m_units[i]->config();
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), c.id);
        s.setValue(QStringLiteral("name"), c.name);
        s.setValue(QStringLiteral("channels"), c.channels);
        s.setValue(QStringLiteral("gain"), c.gain);
        s.setValue(QStringLiteral("muted"), c.muted);
        s.setValue(QStringLiteral("inject"), c.inject.node);
        s.setValue(QStringLiteral("injectLabel"), c.inject.description);
        s.setValue(QStringLiteral("injectMonitor"), c.inject.monitor);
    }
    s.endArray();
}

void MainWindow::restoreSession()
{
    m_restoring = true;
    QSettings s;
    const int n = s.beginReadArray(QStringLiteral("cables"));
    QStringList failed;
    for (int i = 0; i < n; ++i)
    {
        s.setArrayIndex(i);
        CableConfig c;
        c.id = s.value(QStringLiteral("id")).toString();
        c.name = s.value(QStringLiteral("name")).toString();
        c.channels = s.value(QStringLiteral("channels"), 2).toInt();
        c.gain = s.value(QStringLiteral("gain"), 1.0f).toFloat();
        c.muted = s.value(QStringLiteral("muted"), false).toBool();
        c.inject.node = s.value(QStringLiteral("inject")).toString();
        c.inject.description = s.value(QStringLiteral("injectLabel"), c.inject.node).toString();
        c.inject.monitor = s.value(QStringLiteral("injectMonitor"), false).toBool();
        if (c.id.isEmpty() || c.name.isEmpty())
            continue;
        if (!addUnit(c, false))
            failed.append(c.name);
    }
    s.endArray();
    m_restoring = false;

    if (!failed.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Some cables could not be restored"),
                             QStringLiteral("These saved cables failed to come back:\n\n%1")
                                 .arg(failed.join(QStringLiteral(", "))));

    updateEmptyState();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    saveSession();
    for (RackUnit *u : m_units)
        closeCable(u);
    m_units.clear();
    e->accept();
}

void MainWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Theme::windowBg());
}
