#include "LoginDialog.h"

#include "LoadingSpinner.h"

#include <QAbstractItemModel>
#include <QButtonGroup>
#include <QComboBox>
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
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QStyleOptionComboBox>
#include <QStylePainter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#include <utility>

namespace PicoATE::Ui {

namespace {

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
    brand->setAccessibleName(tr("PICO"));
    brand->setAlignment(Qt::AlignCenter);
    brand->setFixedSize(220, 180);
    const QPixmap brandImage(
        QStringLiteral(":/branding/PicoATE-Lockup-Vertical.png"));
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

    m_sequenceCombo = new CenteredComboBox(card);
    m_sequenceCombo->setObjectName(QStringLiteral("loginSequenceCombo"));
    m_sequenceCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_sequenceCombo->setMinimumContentsLength(36);
    m_sequenceCombo->setMinimumHeight(40);
    fields->addWidget(m_sequenceCombo);

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
        QFrame#loginCard QLabel { color: #3d4248; background: transparent; }
        QLabel#loginBrand { color: #25292e; }
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
    connect(m_sequenceCombo, &QComboBox::currentIndexChanged,
            this, &LoginDialog::updateStationPath);
    connect(m_loginButton, &QPushButton::clicked,
            this, &LoginDialog::submit);
    connect(m_passwordEdit, &QLineEdit::returnPressed,
            this, &LoginDialog::submit);
    connect(m_passwordEdit, &QLineEdit::textEdited, this, [this](QString text) {
        if (m_passwordError) {
            text.remove(tr("Admin 密码错误"));
            setPasswordError(false);
            if (m_passwordEdit->text() != text) {
                m_passwordEdit->setText(text);
            }
            m_passwordEdit->setCursorPosition(text.size());
        }
    });
    connect(m_closeButton, &QToolButton::clicked,
            this, &QDialog::reject);

    populateSequences();
    updateModeUi();
    updateStationPath();
}

StartupSelection LoginDialog::selection() const
{
    return m_selection;
}

void LoginDialog::setInitialSequencePath(const QString& filePath)
{
    const auto absolutePath = QFileInfo(filePath).absoluteFilePath();
    int index = m_sequenceCombo->findData(absolutePath);
    if (index < 0 && QFileInfo::exists(absolutePath)) {
        m_sequenceCombo->addItem(QFileInfo(absolutePath).fileName(), absolutePath);
        index = m_sequenceCombo->count() - 1;
    }
    if (index >= 0) {
        m_sequenceCombo->setCurrentIndex(index);
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
    m_loginButton->setText(admin ? tr("Open Admin") : tr("Start Test"));
    m_errorLabel->hide();
    updateDialogGeometry();
}

void LoginDialog::updateStationPath()
{
    m_loginButton->setEnabled(m_sequenceCombo->currentIndex() >= 0);
}

void LoginDialog::submit()
{
    if (m_busy) {
        return;
    }
    const auto mode = selectedMode();
    const auto sequencePath = m_sequenceCombo->currentData().toString();
    const auto stationPath = StartupSupport::stationPathForSequence(sequencePath);
    const auto validation = StartupSupport::validateSelection(
        mode, sequencePath, stationPath, m_passwordEdit->text());
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
    m_selection.sequencePath = QFileInfo(sequencePath).absoluteFilePath();
    m_selection.stationPath = QFileInfo(stationPath).absoluteFilePath();
    m_selection.scanDialogEnabled = StartupSupport::stationScanDialogEnabled(
        m_selection.stationPath);
    m_selection.snValidationRules =
        StartupSupport::stationSnValidationRules(m_selection.stationPath);
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
    for (const auto& filePath :
         StartupSupport::discoverSequenceFiles(m_sequenceRootDirectory)) {
        m_sequenceCombo->addItem(QFileInfo(filePath).fileName(), filePath);
    }
    centerComboItems(m_sequenceCombo);
    if (m_sequenceCombo->count() == 0) {
        showError(tr("No Sequence JSON was found in %1")
                      .arg(m_sequenceRootDirectory));
    }
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
    m_sequenceCombo->setEnabled(!busy);
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
    updateDialogGeometry();
}

void LoginDialog::setPasswordError(bool invalid)
{
    const auto errorText = tr("Admin 密码错误");
    const bool wasInvalid = m_passwordError;
    if (!invalid && wasInvalid && m_passwordEdit->text() == errorText) {
        m_passwordEdit->clear();
    }

    m_passwordError = invalid;
    m_passwordEdit->setProperty("invalid", invalid);
    if (invalid) {
        m_passwordEdit->clear();
        m_passwordEdit->setEchoMode(QLineEdit::Normal);
        m_passwordEdit->setPlaceholderText({});
        m_passwordEdit->setText(errorText);
        m_passwordEdit->setCursorPosition(errorText.size());
    } else {
        m_passwordEdit->setEchoMode(QLineEdit::Password);
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

    int targetHeight = 442;
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

} // namespace PicoATE::Ui
