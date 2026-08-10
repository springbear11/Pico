#include "LoginDialog.h"

#include "LoadingSpinner.h"
#include "PicoATE/Core/ProductRouting.h"

#include <QAbstractItemModel>
#include <QButtonGroup>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLayout>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <algorithm>
#include <utility>

namespace PicoATE::Ui {

namespace {

constexpr int ProjectPathRole = Qt::UserRole;
constexpr int SequencePathRole = Qt::UserRole + 1;
constexpr int StationPathRole = Qt::UserRole + 2;
constexpr int NewProjectTemplateRole = Qt::UserRole + 3;

class CenteredComboBox final : public QComboBox
{
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStylePainter painter(this);
        QStyleOptionComboBox option;
        initStyleOption(&option);
        painter.drawComplexControl(QStyle::CC_ComboBox, option);
        painter.setPen(palette().color(isEnabled()
                                           ? QPalette::Active
                                           : QPalette::Disabled,
                                       QPalette::Text));
        painter.drawText(rect().adjusted(28, 0, -28, 0),
                         Qt::AlignCenter,
                         option.currentText);

        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen arrowPen(QColor(157, 163, 170), 1.1,
                      Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(arrowPen);
        const QPointF center(width() - 17.0, height() / 2.0);
        painter.drawLine(center + QPointF(-3.0, -1.0),
                         center + QPointF(0.0, 2.0));
        painter.drawLine(center + QPointF(0.0, 2.0),
                         center + QPointF(3.0, -1.0));
    }
};

void centerComboItems(QComboBox* combo)
{
    for (int index = 0; index < combo->count(); ++index) {
        combo->model()->setData(combo->model()->index(index, 0),
                                Qt::AlignCenter,
                                Qt::TextAlignmentRole);
    }
}

QPixmap tintedLoginIcon(const QString& resourcePath,
                        const QSize& size,
                        const QColor& color,
                        int rightPadding = 0)
{
    const auto source = QIcon(resourcePath).pixmap(size);
    if (source.isNull()) {
        return {};
    }
    const auto devicePixelRatio = source.devicePixelRatio();
    QPixmap result(source.width()
                       + qRound(rightPadding * devicePixelRatio),
                   source.height());
    result.setDevicePixelRatio(devicePixelRatio);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    painter.drawPixmap(0, 0, source);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(result.rect(), color);
    return result;
}

QIcon autoBySnToggleIcon()
{
    constexpr int IconTextGap = 8;
    QIcon icon;
    for (const auto& size : {QSize(15, 15), QSize(18, 18)}) {
        icon.addPixmap(
            tintedLoginIcon(QStringLiteral(":/icons/circle.svg"), size,
                            QColor(QStringLiteral("#7b838b")), IconTextGap),
            QIcon::Normal, QIcon::Off);
        icon.addPixmap(
            tintedLoginIcon(QStringLiteral(":/icons/circle.svg"), size,
                            QColor(QStringLiteral("#34383e")), IconTextGap),
            QIcon::Active, QIcon::Off);
        icon.addPixmap(
            tintedLoginIcon(QStringLiteral(":/icons/circle.svg"), size,
                            QColor(QStringLiteral("#b7bdc3")), IconTextGap),
            QIcon::Disabled, QIcon::Off);
        const auto checked = tintedLoginIcon(
            QStringLiteral(":/icons/circle-check.svg"), size, Qt::white,
            IconTextGap);
        icon.addPixmap(checked, QIcon::Normal, QIcon::On);
        icon.addPixmap(checked, QIcon::Active, QIcon::On);
        icon.addPixmap(checked, QIcon::Disabled, QIcon::On);
    }
    return icon;
}

QString preferredLoginFontFamily()
{
    const QStringList candidates = {
        QStringLiteral("Bahnschrift"),
        QStringLiteral("Segoe UI Variable Display"),
        QStringLiteral("Segoe UI")};
    for (const auto& candidate : candidates) {
        if (QFontDatabase::hasFamily(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace

LoginDialog::LoginDialog(QString sequenceRootDirectory, QWidget* parent)
    : QDialog(parent)
    , m_sequenceRootDirectory(std::move(sequenceRootDirectory))
{
    setObjectName(QStringLiteral("loginDialog"));
    setWindowTitle(tr("PicoATE Login"));
    setModal(true);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedWidth(560);

    m_productRoutingPath = StartupSupport::productRoutingPathForRoot(
        m_sequenceRootDirectory);
    const auto rootKey = QCryptographicHash::hash(
        QFileInfo(m_sequenceRootDirectory).absoluteFilePath().toUtf8(),
        QCryptographicHash::Sha1).toHex();
    m_settingsGroup = QStringLiteral("Login/%1")
                          .arg(QString::fromLatin1(rootKey));
    refreshRoutingPolicy();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(0);

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("loginCard"));
    auto* shadow = new QGraphicsDropShadowEffect(card);
    shadow->setBlurRadius(36.0);
    shadow->setOffset(0.0, 10.0);
    shadow->setColor(QColor(32, 38, 45, 70));
    card->setGraphicsEffect(shadow);
    layout->addWidget(card);

    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(42, 22, 42, 28);
    cardLayout->setSpacing(12);

    QFont interfaceFont = card->font();
    const auto preferredFamily = preferredLoginFontFamily();
    if (!preferredFamily.isEmpty()) {
        interfaceFont.setFamily(preferredFamily);
    }
    interfaceFont.setPointSizeF(10.5);
    interfaceFont.setStyleStrategy(QFont::PreferAntialias);
    card->setFont(interfaceFont);

    m_header = new QFrame(card);
    m_header->setObjectName(QStringLiteral("loginHeader"));
    auto* headerLayout = new QHBoxLayout(m_header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);

    auto* headerBalance = new QWidget(m_header);
    headerBalance->setFixedSize(32, 32);
    headerLayout->addWidget(headerBalance, 0, Qt::AlignTop);
    headerLayout->addStretch();

    auto* brand = new QLabel(m_header);
    brand->setObjectName(QStringLiteral("loginBrand"));
    brand->setAccessibleName(tr("SINEXCEL"));
    brand->setAlignment(Qt::AlignCenter);
    brand->setFixedSize(235, 44);
    const QPixmap brandImage(
        QStringLiteral(":/branding/Sinexcel.png"));
    const qreal pixelRatio = devicePixelRatioF();
    QPixmap scaledBrand = brandImage.scaled(
        QSize(qRound(brand->width() * pixelRatio),
              qRound(brand->height() * pixelRatio)),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation);
    scaledBrand.setDevicePixelRatio(pixelRatio);
    brand->setPixmap(scaledBrand);
    headerLayout->addWidget(brand);
    headerLayout->addStretch();

    m_closeButton = new QToolButton(m_header);
    m_closeButton->setObjectName(QStringLiteral("loginCloseButton"));
    m_closeButton->setText(QStringLiteral("X"));
    m_closeButton->setToolTip(tr("Close"));
    m_closeButton->setAutoRaise(true);
    m_closeButton->setFixedSize(32, 32);
    headerLayout->addWidget(m_closeButton, 0, Qt::AlignTop);
    cardLayout->addWidget(m_header);
    cardLayout->addSpacing(4);

    auto* fields = new QVBoxLayout;
    fields->setContentsMargins(0, 0, 0, 0);
    fields->setSpacing(12);

    auto* modeRow = new QHBoxLayout;
    modeRow->setContentsMargins(0, 0, 0, 0);
    modeRow->addStretch();
    auto* modeSelector = new QFrame(card);
    modeSelector->setObjectName(QStringLiteral("loginModeSelector"));
    modeSelector->setFixedSize(154, 44);
    auto* modeLayout = new QHBoxLayout(modeSelector);
    modeLayout->setContentsMargins(4, 4, 4, 4);
    modeLayout->setSpacing(2);

    m_testModeButton = new QToolButton(modeSelector);
    m_testModeButton->setObjectName(QStringLiteral("loginTestModeButton"));
    m_testModeButton->setCheckable(true);
    m_testModeButton->setAutoRaise(true);
    m_testModeButton->setFixedSize(72, 36);
    m_testModeButton->setIconSize(QSize(22, 22));
    m_testModeButton->setAccessibleName(tr("Test mode"));
    m_testModeButton->setToolTip(tr("Test mode - operator"));
    modeLayout->addWidget(m_testModeButton);

    m_adminModeButton = new QToolButton(modeSelector);
    m_adminModeButton->setObjectName(QStringLiteral("loginAdminModeButton"));
    m_adminModeButton->setCheckable(true);
    m_adminModeButton->setAutoRaise(true);
    m_adminModeButton->setFixedSize(72, 36);
    m_adminModeButton->setIconSize(QSize(22, 22));
    m_adminModeButton->setAccessibleName(tr("Admin mode"));
    m_adminModeButton->setToolTip(tr("Admin mode - engineer"));
    modeLayout->addWidget(m_adminModeButton);

    auto* modeGroup = new QButtonGroup(this);
    modeGroup->setExclusive(true);
    modeGroup->addButton(m_testModeButton, int(UiMode::Test));
    modeGroup->addButton(m_adminModeButton, int(UiMode::Admin));
    m_testModeButton->setChecked(true);
    modeRow->addWidget(modeSelector);
    modeRow->addStretch();
    fields->addLayout(modeRow);

    auto* loadModeRow = new QHBoxLayout;
    loadModeRow->setContentsMargins(0, 0, 0, 0);
    loadModeRow->addStretch();
    m_autoLoadButton = new QToolButton(card);
    m_autoLoadButton->setObjectName(QStringLiteral("loginAutoBySnButton"));
    m_autoLoadButton->setText(tr("AUTO BY SN"));
    m_autoLoadButton->setCheckable(true);
    m_autoLoadButton->setAutoRaise(false);
    m_autoLoadButton->setIcon(autoBySnToggleIcon());
    m_autoLoadButton->setIconSize(QSize(23, 15));
    m_autoLoadButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_autoLoadButton->setFixedSize(136, 34);
    m_autoLoadButton->setAccessibleName(tr("Automatic routing by serial number"));
    loadModeRow->addWidget(m_autoLoadButton);
    loadModeRow->addStretch();
    fields->addLayout(loadModeRow);

    m_sequenceCombo = new CenteredComboBox(card);
    m_sequenceCombo->setObjectName(QStringLiteral("loginSequenceCombo"));
    m_sequenceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sequenceCombo->setMinimumContentsLength(36);
    m_sequenceCombo->setMinimumHeight(40);

    m_sequenceStack = new QStackedWidget(card);
    m_sequenceStack->setObjectName(QStringLiteral("loginSequenceStack"));
    m_sequenceStack->setFixedHeight(40);
    auto* autoRouteLabel = new QLabel(tr("Project selected after SN scan"),
                                      m_sequenceStack);
    autoRouteLabel->setObjectName(QStringLiteral("loginAutoRouteHint"));
    autoRouteLabel->setAlignment(Qt::AlignCenter);
    m_sequenceStack->addWidget(autoRouteLabel);
    m_sequenceStack->addWidget(m_sequenceCombo);
    fields->addWidget(m_sequenceStack);

    m_passwordEdit = new QLineEdit(card);
    m_passwordEdit->setObjectName(QStringLiteral("loginAdminPassword"));
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText(tr("Admin password"));
    m_passwordEdit->setMinimumHeight(40);
    m_passwordEdit->setAlignment(Qt::AlignCenter);
    fields->addWidget(m_passwordEdit);
    cardLayout->addLayout(fields);

    m_errorLabel = new QLabel(card);
    m_errorLabel->setObjectName(QStringLiteral("loginErrorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->hide();
    cardLayout->addWidget(m_errorLabel);

    auto* busyRow = new QWidget(card);
    busyRow->setObjectName(QStringLiteral("loginBusyRow"));
    auto* busyLayout = new QHBoxLayout(busyRow);
    busyLayout->setContentsMargins(0, 0, 0, 0);
    busyLayout->setSpacing(10);
    busyLayout->addStretch();
    m_spinner = new LoadingSpinner(busyRow);
    m_spinner->setObjectName(QStringLiteral("loginLoadingSpinner"));
    m_spinner->setColor(QColor(63, 68, 75));
    m_spinner->setRunning(false);
    busyLayout->addWidget(m_spinner);
    m_statusLabel = new QLabel(busyRow);
    m_statusLabel->setObjectName(QStringLiteral("loginStatusLabel"));
    m_statusLabel->setAlignment(Qt::AlignCenter);
    busyLayout->addWidget(m_statusLabel);
    busyLayout->addStretch();
    busyRow->hide();
    cardLayout->addWidget(busyRow);
    cardLayout->addSpacing(18);

    m_loginButton = new QPushButton(tr("Start Test"), card);
    m_loginButton->setObjectName(QStringLiteral("loginButton"));
    m_loginButton->setDefault(true);
    m_loginButton->setMinimumHeight(44);
    cardLayout->addWidget(m_loginButton);

    setStyleSheet(QStringLiteral(R"css(
        QFrame#loginCard {
            background: #ffffff;
            border: 1px solid #d7dbe0;
            border-radius: 7px;
        }
        QFrame#loginHeader { border: none; background: transparent; }
        QFrame#loginModeSelector {
            background: #eef0f3;
            border: 1px solid #d7dbe0;
            border-radius: 22px;
        }
        QToolButton#loginTestModeButton,
        QToolButton#loginAdminModeButton {
            background: transparent;
            border: none;
            border-radius: 18px;
        }
        QToolButton#loginTestModeButton:hover:unchecked,
        QToolButton#loginAdminModeButton:hover:unchecked {
            background: #e0e4e8;
        }
        QToolButton#loginTestModeButton:checked,
        QToolButton#loginAdminModeButton:checked {
            background: #34383e;
        }
        QToolButton#loginAutoBySnButton {
            color: #34383e;
            background: #ffffff;
            border: 1px solid #c9cdd3;
            border-radius: 17px;
            font-weight: 700;
        }
        QToolButton#loginAutoBySnButton:hover:unchecked {
            color: #24272b;
            background: #f2f3f5;
            border-color: #9fa6ae;
        }
        QToolButton#loginAutoBySnButton:checked {
            color: #ffffff;
            background: #34383e;
            border-color: #34383e;
        }
        QToolButton#loginAutoBySnButton:disabled:unchecked {
            color: #aeb3b9;
            background: #f2f3f5;
            border-color: #d7dbe0;
        }
        QToolButton#loginAutoBySnButton:checked:disabled {
            color: #ffffff;
            background: #34383e;
            border-color: #34383e;
        }
        QFrame#loginCard QLabel { color: #3d4248; background: transparent; }
        QLabel#loginBrand { color: #25292e; }
        QLabel#loginAutoRouteHint {
            color: #6f767e;
            background: #f5f6f8;
            border: 1px solid #c9cdd3;
            border-radius: 5px;
            font-weight: 600;
        }
        QLabel#loginStatusLabel { color: #858b93; }
        QLabel#loginErrorLabel {
            color: #9e2924;
            background: #fff1f0;
            border: 1px solid #efc4c1;
            border-radius: 5px;
            padding: 7px 10px;
        }
        QComboBox, QLineEdit {
            color: #30343a;
            background: #f5f6f8;
            border: 1px solid #c9cdd3;
            border-radius: 5px;
            padding: 0 11px;
            selection-background-color: #cfd5dc;
        }
        QComboBox { padding: 0 28px; }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 24px;
            border: none;
            background: transparent;
        }
        QComboBox::down-arrow { image: none; width: 0; height: 0; }
        QComboBox:focus, QLineEdit:focus {
            background: #ffffff;
            border: 1px solid #686e76;
        }
        QLineEdit#loginAdminPassword[invalid="true"] {
            color: #a83530;
            background: #fff7f6;
            border: 1px solid #d95c56;
        }
        QComboBox:disabled, QLineEdit:disabled {
            color: #8d9299;
            background: #eceef1;
            border-color: #d7dbe0;
        }
        QComboBox QAbstractItemView {
            color: #30343a;
            background: #ffffff;
            border: 1px solid #c9cdd3;
            selection-color: #202328;
            selection-background-color: #e2e5e9;
            outline: none;
        }
        QPushButton#loginButton {
            color: #ffffff;
            background: #34383e;
            border: 1px solid #34383e;
            border-radius: 5px;
            font-weight: 700;
            padding: 0 18px;
        }
        QPushButton#loginButton:hover { background: #24272b; border-color: #24272b; }
        QPushButton#loginButton:pressed { background: #17191c; border-color: #17191c; }
        QPushButton#loginButton:disabled {
            color: #f0f1f3;
            background: #aeb3b9;
            border-color: #aeb3b9;
        }
        QToolButton#loginCloseButton {
            color: #565c64;
            border: none;
            border-radius: 5px;
            background: transparent;
            font-weight: 600;
        }
        QToolButton#loginCloseButton:hover { color: #202328; background: #eceef1; }
        QToolButton#loginCloseButton:pressed { background: #dde0e4; }
    )css"));

    connect(modeGroup, &QButtonGroup::idClicked,
            this, &LoginDialog::updateModeUi);
    connect(m_autoLoadButton, &QToolButton::toggled,
            this, &LoginDialog::updateLoadModeUi);
    connect(m_sequenceCombo, &QComboBox::currentIndexChanged,
            this, &LoginDialog::updateStationPath);
    connect(m_loginButton, &QPushButton::clicked,
            this, &LoginDialog::submit);
    connect(m_passwordEdit, &QLineEdit::returnPressed,
            this, &LoginDialog::submit);
    connect(m_passwordEdit, &QLineEdit::textEdited, this, [this] {
        if (m_passwordError) {
            setPasswordError(false);
        }
    });
    connect(m_closeButton, &QToolButton::clicked,
            this, &QDialog::reject);

    m_restoringPreferences = true;
    populateSequences();
    m_restoringPreferences = false;
    updateModeUi();
    updateStationPath();
}

std::unique_ptr<LoginDialog> createLoginDialog(QString sequenceRootDirectory)
{
    return std::make_unique<LoginDialog>(std::move(sequenceRootDirectory));
}

StartupSelection LoginDialog::selection() const
{
    return m_selection;
}

void LoginDialog::setInitialSequencePath(const QString& filePath)
{
    const auto absolutePath = QFileInfo(filePath).absoluteFilePath();
    int index = m_sequenceCombo->findData(absolutePath, SequencePathRole);
    if (index < 0 && QFileInfo::exists(absolutePath)) {
        const auto stationPath = StartupSupport::stationPathForSequence(
            absolutePath);
        m_sequenceCombo->addItem(QFileInfo(absolutePath).fileName());
        index = m_sequenceCombo->count() - 1;
        m_sequenceCombo->setItemData(
            index, QFileInfo(absolutePath).absolutePath(), ProjectPathRole);
        m_sequenceCombo->setItemData(index, absolutePath, SequencePathRole);
        m_sequenceCombo->setItemData(index, stationPath, StationPathRole);
    }
    if (index >= 0) {
        m_sequenceCombo->setCurrentIndex(index);
        const bool manualAllowed = selectedMode() == UiMode::Admin ||
            !m_routingFilePresent ||
            (m_routingValid && m_allowManualInTest);
        if (manualAllowed) {
            m_autoLoadButton->setChecked(false);
            updateLoadModeUi();
        }
    }
}

void LoginDialog::updateModeUi()
{
    const bool admin = selectedMode() == UiMode::Admin;
    m_testModeButton->setIcon(QIcon(admin
        ? QStringLiteral(":/branding/Mode-Operator.png")
        : QStringLiteral(":/branding/Mode-Operator-Selected.png")));
    m_adminModeButton->setIcon(QIcon(admin
        ? QStringLiteral(":/branding/Mode-Engineer-Selected.png")
        : QStringLiteral(":/branding/Mode-Engineer.png")));
    m_passwordEdit->setVisible(admin);
    setPasswordError(false);
    if (!admin) {
        m_passwordEdit->clear();
    }
    restorePreferencesForMode();
    updateLoadModeUi();
    m_loginButton->setText(admin ? tr("Open Admin") : tr("Start Test"));
    m_errorLabel->hide();
    updateDialogGeometry();
    if (admin) {
        QTimer::singleShot(0, this, [this] {
            if (!m_busy && selectedMode() == UiMode::Admin &&
                m_passwordEdit->isVisible()) {
                m_passwordEdit->setFocus(Qt::OtherFocusReason);
                m_passwordEdit->setCursorPosition(m_passwordEdit->text().size());
            }
        });
    }
}

void LoginDialog::updateLoadModeUi()
{
    const bool admin = selectedMode() == UiMode::Admin;
    const bool manualAllowed = admin || !m_routingFilePresent ||
        !m_routingHasEnabledRoutes ||
        (m_routingValid && m_allowManualInTest);
    const bool autoAvailable = m_routingValid && m_routingHasEnabledRoutes;

    if (!manualAllowed && !m_autoLoadButton->isChecked()) {
        m_autoLoadButton->setChecked(true);
    } else if (!autoAvailable && m_autoLoadButton->isChecked()) {
        m_autoLoadButton->setChecked(false);
    }

    m_autoLoadButton->setEnabled(
        !m_busy && autoAvailable && manualAllowed);
    if (!m_routingFilePresent) {
        m_autoLoadButton->setToolTip(
            tr("ProductRouting.json is not available; manual mode is active"));
    } else if (!autoAvailable) {
        m_autoLoadButton->setToolTip(
            tr("No enabled product routes are configured; manual mode is active"));
    } else if (!manualAllowed) {
        m_autoLoadButton->setToolTip(
            tr("Automatic routing is required by ProductRouting.json"));
    } else if (m_autoLoadButton->isChecked()) {
        m_autoLoadButton->setToolTip(
            tr("Automatic routing is active. Click to choose a project manually"));
    } else {
        m_autoLoadButton->setToolTip(
            tr("Manual mode is active. Click to route by scanned SN"));
    }
    m_sequenceStack->setCurrentIndex(
        selectedLoadMode() == SequenceLoadMode::AutoBySn ? 0 : 1);
    m_errorLabel->hide();
    if (!m_restoringPreferences) {
        savePreferences();
    }
    updateStationPath();
}

void LoginDialog::updateStationPath()
{
    const bool newProjectTemplate =
        m_sequenceCombo->currentData(NewProjectTemplateRole).toBool();
    const bool canSubmit = selectedLoadMode() == SequenceLoadMode::AutoBySn
        ? m_routingValid && m_routingHasEnabledRoutes
        : m_sequenceCombo->currentIndex() >= 0 &&
              (newProjectTemplate ||
               (!m_sequenceCombo->currentData(SequencePathRole)
                     .toString().isEmpty() &&
                !m_sequenceCombo->currentData(StationPathRole)
                     .toString().isEmpty()));
    m_loginButton->setEnabled(!m_busy && canSubmit);
    if (!m_restoringPreferences) {
        savePreferences();
    }
}

void LoginDialog::submit()
{
    if (m_busy) {
        return;
    }
    const auto mode = selectedMode();
    const auto loadMode = selectedLoadMode();
    const bool newProjectTemplate = loadMode == SequenceLoadMode::Manual &&
        m_sequenceCombo->currentData(NewProjectTemplateRole).toBool();
    const auto projectPath = loadMode == SequenceLoadMode::Manual
        ? m_sequenceCombo->currentData(ProjectPathRole).toString()
        : QString{};
    const auto sequencePath = loadMode == SequenceLoadMode::Manual
        ? m_sequenceCombo->currentData(SequencePathRole).toString()
        : QString{};
    const auto stationPath = loadMode == SequenceLoadMode::Manual
        ? m_sequenceCombo->currentData(StationPathRole).toString()
        : QString{};
    StartupValidationResult validation;
    if (newProjectTemplate) {
        if (mode == UiMode::Test) {
            validation.errors.push_back(
                tr("No saved product project is available. Use Admin mode to create and save one first."));
        }
        if (mode == UiMode::Admin &&
            !StartupSupport::matchesDailyAdminPassword(
                m_passwordEdit->text())) {
            validation.errors.push_back(QStringLiteral("Admin 密码错误"));
        }
    } else {
        validation = loadMode == SequenceLoadMode::Manual
            ? StartupSupport::validateSelection(
                  mode, sequencePath, stationPath, m_passwordEdit->text())
            : StartupSupport::validateAutoSelection(
                  mode, m_productRoutingPath, {}, m_passwordEdit->text());
    }
    if (!validation.ok()) {
        QStringList remainingErrors = validation.errors;
        const bool invalidPassword =
            mode == UiMode::Admin &&
            !StartupSupport::matchesDailyAdminPassword(m_passwordEdit->text());
        if (invalidPassword) {
            remainingErrors.removeAll(QStringLiteral("Admin 密码错误"));
        }
        setPasswordError(invalidPassword);
        if (remainingErrors.isEmpty()) {
            m_errorLabel->hide();
            updateDialogGeometry();
        } else {
            showError(remainingErrors.join(QStringLiteral("\n")));
        }
        if (mode == UiMode::Admin) {
            m_passwordEdit->setFocus(Qt::OtherFocusReason);
        }
        return;
    }
    setPasswordError(false);

    m_selection.mode = mode;
    m_selection.sequenceLoadMode = loadMode;
    m_selection.newProjectTemplate = newProjectTemplate;
    m_selection.projectRootPath = QFileInfo(m_projectRootPath)
                                      .absoluteFilePath();
    m_selection.projectPath = projectPath.isEmpty()
        ? QString{}
        : QFileInfo(projectPath).absoluteFilePath();
    m_selection.projectName = projectPath.isEmpty()
        ? QString{}
        : QFileInfo(projectPath).fileName();
    m_selection.sequencePath = sequencePath.isEmpty()
        ? QString{}
        : QFileInfo(sequencePath).absoluteFilePath();
    m_selection.stationPath = stationPath.isEmpty()
        ? QString{}
        : QFileInfo(stationPath).absoluteFilePath();
    m_selection.productRoutingPath = QFileInfo(m_productRoutingPath)
                                         .absoluteFilePath();
    m_selection.scanDialogEnabled = newProjectTemplate ||
        loadMode == SequenceLoadMode::AutoBySn ||
        StartupSupport::stationScanDialogEnabled(m_selection.stationPath);
    m_selection.snValidationRules = newProjectTemplate
        ? SnValidationRules{0, {}, QStringLiteral("^[A-Z0-9]+$")}
        : StartupSupport::stationSnValidationRules(m_selection.stationPath);
    savePreferences();
    if (mode == UiMode::Admin) {
        m_errorLabel->hide();
        setBusy(true, tr("Preparing the Admin workspace..."));
        QTimer::singleShot(120, this, [this] { accept(); });
        return;
    }
    accept();
}

void LoginDialog::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_header && windowHandle()) {
        const auto headerPosition = m_header->mapFrom(
            this, event->position().toPoint());
        if (m_header->rect().contains(headerPosition) &&
            !m_closeButton->geometry().contains(
                m_closeButton->parentWidget()->mapFrom(
                    this, event->position().toPoint()))) {
            windowHandle()->startSystemMove();
            event->accept();
            return;
        }
    }
    QDialog::mousePressEvent(event);
}

void LoginDialog::populateSequences()
{
    m_sequenceCombo->clear();
    const auto projects = PicoATE::Core::discoverProductProjects(
        m_projectRootPath);
    for (const auto& project : projects) {
        if (!project.ok()) {
            continue;
        }
        const int index = m_sequenceCombo->count();
        m_sequenceCombo->addItem(project.name);
        m_sequenceCombo->setItemData(index, project.directoryPath,
                                     ProjectPathRole);
        m_sequenceCombo->setItemData(index, project.sequencePath,
                                     SequencePathRole);
        m_sequenceCombo->setItemData(index, project.stationPath,
                                     StationPathRole);
        m_sequenceCombo->setItemData(
            index,
            tr("%1\nSequence: %2\nStation: %3")
                .arg(project.directoryPath,
                     QFileInfo(project.sequencePath).fileName(),
                     QFileInfo(project.stationPath).fileName()),
            Qt::ToolTipRole);
    }

    // Keep existing flat toolkits usable while projects are migrated.
    if (m_sequenceCombo->count() == 0) {
        for (const auto& filePath :
             StartupSupport::discoverSequenceFiles(m_sequenceRootDirectory)) {
            const int index = m_sequenceCombo->count();
            const auto stationPath = StartupSupport::stationPathForSequence(
                filePath);
            m_sequenceCombo->addItem(QFileInfo(filePath).fileName());
            m_sequenceCombo->setItemData(
                index, QFileInfo(filePath).absolutePath(), ProjectPathRole);
            m_sequenceCombo->setItemData(index, filePath, SequencePathRole);
            m_sequenceCombo->setItemData(index, stationPath, StationPathRole);
        }
    }
    if (m_sequenceCombo->count() == 0) {
        const int index = m_sequenceCombo->count();
        m_sequenceCombo->addItem(tr("New Project Template"));
        m_sequenceCombo->setItemData(index, true, NewProjectTemplateRole);
        m_sequenceCombo->setItemData(
            index,
            tr("Creates an empty Sequence and Station in Admin mode. The first save creates a new project."),
            Qt::ToolTipRole);
    }
    centerComboItems(m_sequenceCombo);
}

void LoginDialog::refreshRoutingPolicy()
{
    m_projectRootPath = StartupSupport::productProjectRootPathForRoot(
        m_sequenceRootDirectory);
    m_routingFilePresent = QFileInfo::exists(m_productRoutingPath);
    m_routingValid = false;
    m_routingHasEnabledRoutes = false;
    m_allowManualInTest = !m_routingFilePresent;
    if (!m_routingFilePresent) {
        return;
    }

    const auto routing = PicoATE::Core::loadProductRoutingFile(
        m_productRoutingPath);
    m_routingValid = routing.ok();
    if (!routing.ok()) {
        return;
    }
    m_projectRootPath = routing.config.projectRootPath;
    m_allowManualInTest = routing.config.allowManualInTest;
    m_routingHasEnabledRoutes = std::any_of(
        routing.config.routes.cbegin(),
        routing.config.routes.cend(),
        [](const auto& route) { return route.enabled; });
}

void LoginDialog::restorePreferencesForMode()
{
    m_restoringPreferences = true;
    QSettings settings;
    settings.beginGroup(m_settingsGroup);
    const bool admin = selectedMode() == UiMode::Admin;
    const auto key = admin ? QStringLiteral("AdminLoadMode")
                           : QStringLiteral("TestLoadMode");
    const auto fallback = admin ? QStringLiteral("manual")
                                : QStringLiteral("auto");
    const auto storedMode = settings.value(key, fallback).toString();
    const bool manualAllowed = admin || !m_routingFilePresent ||
        !m_routingHasEnabledRoutes ||
        (m_routingValid && m_allowManualInTest);
    const bool wantsManual = storedMode.compare(
        QStringLiteral("manual"), Qt::CaseInsensitive) == 0;
    m_autoLoadButton->setChecked(
        m_routingValid && m_routingHasEnabledRoutes &&
        !(wantsManual && manualAllowed));

    auto lastProject = settings.value(
        QStringLiteral("LastManualProject")).toString();
    int index = m_sequenceCombo->findData(lastProject, ProjectPathRole);
    if (index < 0) {
        const auto lastSequence = settings.value(
            QStringLiteral("LastManualSequence")).toString();
        index = m_sequenceCombo->findData(lastSequence, SequencePathRole);
    }
    if (index >= 0) {
        m_sequenceCombo->setCurrentIndex(index);
    }
    settings.endGroup();
    m_restoringPreferences = false;
}

void LoginDialog::savePreferences() const
{
    if (m_restoringPreferences) {
        return;
    }
    QSettings settings;
    settings.beginGroup(m_settingsGroup);
    const auto key = selectedMode() == UiMode::Admin
        ? QStringLiteral("AdminLoadMode")
        : QStringLiteral("TestLoadMode");
    settings.setValue(
        key,
        selectedLoadMode() == SequenceLoadMode::AutoBySn
            ? QStringLiteral("auto")
            : QStringLiteral("manual"));
    if (m_sequenceCombo->currentIndex() >= 0) {
        settings.setValue(QStringLiteral("LastManualProject"),
                          m_sequenceCombo->currentData(ProjectPathRole));
        settings.setValue(QStringLiteral("LastManualSequence"),
                          m_sequenceCombo->currentData(SequencePathRole));
    }
    settings.endGroup();
}

void LoginDialog::showError(const QString& message)
{
    m_errorLabel->setText(message);
    m_errorLabel->show();
    updateDialogGeometry();
}

void LoginDialog::setBusy(bool busy, const QString& message)
{
    m_busy = busy;
    m_testModeButton->setEnabled(!busy);
    m_adminModeButton->setEnabled(!busy);
    m_autoLoadButton->setEnabled(false);
    m_sequenceCombo->setEnabled(!busy &&
        selectedLoadMode() == SequenceLoadMode::Manual);
    m_passwordEdit->setEnabled(!busy);
    m_loginButton->setEnabled(!busy);
    m_closeButton->setEnabled(!busy);
    m_loginButton->setText(busy ? tr("Loading...")
                                : (selectedMode() == UiMode::Admin
                                       ? tr("Open Admin")
                                       : tr("Start Test")));
    m_statusLabel->setText(message);
    m_spinner->setRunning(busy);
    m_statusLabel->parentWidget()->setVisible(busy);
    if (!busy) {
        updateLoadModeUi();
    }
    updateDialogGeometry();
}

void LoginDialog::setPasswordError(bool invalid)
{
    const auto errorText = tr("Admin 密码错误");
    m_passwordError = invalid;
    m_passwordEdit->setProperty("invalid", invalid);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    if (invalid) {
        m_passwordEdit->clear();
        m_passwordEdit->setPlaceholderText(errorText);
        m_passwordEdit->setCursorPosition(0);
    } else {
        m_passwordEdit->setPlaceholderText(tr("Admin password"));
    }

    auto palette = m_passwordEdit->palette();
    palette.setColor(QPalette::PlaceholderText,
                     invalid ? QColor(168, 53, 48)
                             : QColor(141, 146, 153));
    m_passwordEdit->setPalette(palette);
    m_passwordEdit->style()->unpolish(m_passwordEdit);
    m_passwordEdit->style()->polish(m_passwordEdit);
    m_passwordEdit->update();
}

void LoginDialog::updateDialogGeometry()
{
    const bool preserveCenter = isVisible();
    const QPoint previousPosition = pos();
    const int previousHeight = height();

    int targetHeight = 398;
    if (m_passwordEdit && !m_passwordEdit->isHidden()) {
        targetHeight += 52;
    }
    if (m_errorLabel && !m_errorLabel->isHidden()) {
        const int errorHeight = qMax(40, m_errorLabel->heightForWidth(444));
        targetHeight += errorHeight + 12;
    }
    if (m_statusLabel && m_statusLabel->parentWidget() &&
        !m_statusLabel->parentWidget()->isHidden()) {
        targetHeight += qMax(28,
                             m_statusLabel->parentWidget()->sizeHint().height())
                        + 12;
    }

    setFixedHeight(targetHeight);
    if (layout()) {
        layout()->activate();
    }

    if (preserveCenter && previousHeight != targetHeight) {
        move(previousPosition.x(),
             previousPosition.y() + (previousHeight - targetHeight) / 2);
    }

}

UiMode LoginDialog::selectedMode() const
{
    return m_adminModeButton && m_adminModeButton->isChecked()
        ? UiMode::Admin
        : UiMode::Test;
}

SequenceLoadMode LoginDialog::selectedLoadMode() const
{
    return m_autoLoadButton && m_autoLoadButton->isChecked()
        ? SequenceLoadMode::AutoBySn
        : SequenceLoadMode::Manual;
}

} // namespace PicoATE::Ui
