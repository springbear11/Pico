#include "IntegrityPage.h"
#include "UiTextBinding.h"

#include <QClipboard>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace PicoATE::Ui {
namespace {
class IntegrityItemDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter* painter, const QStyleOptionViewItem& source, const QModelIndex& index) const override
    {
        QStyleOptionViewItem option(source);
        initStyleOption(&option, index);
        if (option.state & QStyle::State_Selected) {
            option.state &= ~QStyle::State_Selected;
            option.backgroundBrush = QColor("#e1edf2");
        }
        const auto* widget = option.widget;
        (widget ? widget->style() : QApplication::style())->drawControl(
            QStyle::CE_ItemViewItem, &option, painter, widget);
    }
};
}

IntegrityPage::IntegrityPage(QString directory, AdminAccess access, QWidget* parent)
    : QWidget(parent), m_directory(std::move(directory)), m_access(access)
{
    setObjectName(QStringLiteral("integrityPage"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 18, 20, 18);
    root->setSpacing(14);
    auto* heading = new QHBoxLayout;
    auto* title = makeUiLabel("Integrity Check", this);
    title->setObjectName("integrityTitle");
    heading->addWidget(title);
    heading->addStretch();
    m_summary = new QLabel(this);
    m_summary->setObjectName("integritySummary");
    heading->addWidget(m_summary);
    root->addLayout(heading);

    auto* toolbar = new QHBoxLayout;
    auto* baseline = new QLabel(QStringLiteral("SHA-256  |  IntegrityBaseline.json"), this);
    baseline->setToolTip(QDir(m_directory).filePath(RuntimeIntegrity::baselineFileName()));
    toolbar->addWidget(baseline);
    toolbar->addStretch();
    m_refresh = makeUiButton(QIcon(":/icons/refresh-cw.svg"), "Verify Files", this);
    m_refresh->setObjectName("integrityRefreshButton");
    m_approve = makeUiButton(QIcon(":/icons/circle-check.svg"), "Approve Selected", this);
    m_approve->setObjectName("integrityApproveButton");
    toolbar->addWidget(m_refresh);
    toolbar->addWidget(m_approve);
    root->addLayout(toolbar);

    m_files = new QTableWidget(2, 4, this);
    m_files->setObjectName("integrityFilesTable");
    m_files->setItemDelegate(new IntegrityItemDelegate(m_files));
    m_files->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_files->setMouseTracking(false);
    m_files->setAlternatingRowColors(true);
    m_files->setWordWrap(false);
    m_files->verticalHeader()->hide();
    m_files->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_files->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_files->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_files->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_files->setMinimumHeight(240);
    root->addWidget(m_files, 1);
    m_error = new QLabel(this);
    m_error->setObjectName("integrityError");
    m_error->setWordWrap(true);
    root->addWidget(m_error);
    m_metadata = new QLabel(this);
    m_metadata->setObjectName("integrityMetadata");
    m_metadata->setWordWrap(true);
    m_metadata->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_metadata);
    setStyleSheet(QStringLiteral(R"css(
        QWidget#integrityPage { background: #f4f6f7; color: #20262b; }
        QLabel#integrityTitle { font-size: 20px; font-weight: 700; }
        QLabel#integritySummary { padding: 7px 14px; border-radius: 4px; font-weight: 700; }
        QLabel#integrityMetadata { color: #61717b; }
        QLabel#integrityError { color: #a43838; }
        QPushButton { min-height: 32px; padding: 0 12px; }
        QTableWidget#integrityFilesTable { background: #ffffff; alternate-background-color: #f8fafb;
            border: 1px solid #d8dde1; border-radius: 4px; gridline-color: #e2e6e9;
            selection-background-color: #e1edf2; selection-color: #20262b; }
        QTableWidget#integrityFilesTable::item { padding: 10px; }
        QHeaderView::section { background: #eef1f3; color: #364149; padding: 10px;
            border: 0; border-right: 1px solid #d8dde1; border-bottom: 1px solid #cbd2d7; font-weight: 600; }
    )css"));
    connect(m_refresh, &QPushButton::clicked, this, &IntegrityPage::refresh);
    connect(m_approve, &QPushButton::clicked, this, &IntegrityPage::requestApproval);
    connect(m_files, &QTableWidget::itemSelectionChanged, this, &IntegrityPage::updateButtons);
    connect(&UiLanguage::instance(), &UiLanguage::languageChanged, this, &IntegrityPage::render);
    auto* copy = new QShortcut(QKeySequence::Copy, m_files);
    connect(copy, &QShortcut::activated, this, [this] {
        QStringList rows;
        for (const auto& index : m_files->selectionModel()->selectedRows()) {
            QStringList cells;
            for (int column = 0; column < m_files->columnCount(); ++column) {
                cells.push_back(m_files->item(index.row(), column)->text().remove('\n'));
            }
            rows.push_back(cells.join('\t'));
        }
        QApplication::clipboard()->setText(rows.join('\n'));
    });
    render();
    QTimer::singleShot(0, this, &IntegrityPage::refresh);
}

IntegrityPage::~IntegrityPage()
{
    if (m_cancel) m_cancel->store(true);
    if (m_worker) m_worker->wait();
}

void IntegrityPage::setRunActive(bool active)
{
    m_runActive = active;
    updateButtons();
}

void IntegrityPage::refresh() { startJob(); }

void IntegrityPage::startJob(QStringList files, QString password, QString reason)
{
    if (m_worker || (!files.isEmpty() && m_runActive)) return;
    m_cancel = std::make_shared<std::atomic_bool>(false);
    struct Result { IntegrityReport report; QString error; };
    auto result = std::make_shared<Result>();
    const auto reviewed = m_report;
    m_worker = QThread::create([result, reviewed, root = m_directory, access = m_access,
                               files, password, reason, cancel = m_cancel] {
        const auto cancelled = [cancel] { return cancel->load(); };
        if (!files.isEmpty()) result->error = RuntimeIntegrity::authorize(reviewed, files, access, password, reason, cancelled);
        result->report = RuntimeIntegrity::check(root, cancelled);
    });
    m_worker->setParent(this);
    auto* thread = m_worker;
    connect(thread, &QThread::finished, this, [this, thread, result] {
        m_worker = nullptr;
        m_report = result->report;
        render();
        if (!result->error.isEmpty()) {
            m_error->setText(result->error);
            m_error->show();
        }
        thread->deleteLater();
        emit verificationFinished();
    });
    render();
    thread->start();
}

void IntegrityPage::updateButtons()
{
    m_refresh->setEnabled(!busy());
    const bool selected = !m_files->selectionModel()->selectedRows().isEmpty();
    m_approve->setEnabled(!busy() && !m_runActive && selected &&
        m_access == AdminAccess::Supervisor && !m_report.files.isEmpty());
}

void IntegrityPage::render()
{
    m_files->setHorizontalHeaderLabels({uiText("File"), uiText("Baseline SHA-256"),
                                      uiText("Current SHA-256"), uiText("Status")});
    const auto names = RuntimeIntegrity::fileNames();
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(12);
    int matched = 0;
    for (int row = 0; row < names.size(); ++row) {
        const auto entry = row < m_report.files.size() ? m_report.files[row] : IntegrityFile{names[row]};
        if (entry.status == IntegrityStatus::Matched) ++matched;
        const auto digestText = [](const QString& hash) {
            return hash.isEmpty() ? QStringLiteral("--") : hash.left(32) + '\n' + hash.mid(32);
        };
        const QStringList values{names[row], digestText(entry.expected), digestText(entry.actual), integrityStatusText(entry.status)};
        for (int column = 0; column < 4; ++column) {
            auto* item = m_files->item(row, column);
            if (!item) { item = new QTableWidgetItem; m_files->setItem(row, column, item); }
            item->setText(values[column]);
            item->setToolTip(column == 0 ? QDir(m_directory).filePath(names[row]) :
                             column == 1 ? entry.expected : column == 2 ? entry.actual : entry.error);
            if (column == 1 || column == 2) item->setFont(font);
            const bool bad = entry.status != IntegrityStatus::Matched;
            item->setForeground(QColor(column == 3 ? (bad ? "#a43838" : "#2f7548") :
                                       (column == 2 && bad ? "#a43838" : "#344048")));
        }
        m_files->setRowHeight(row, 84);
    }
    m_summary->setText(busy() ? uiText("Verifying...") : m_report.passed()
        ? uiText("Verified %1 / %2").arg(matched).arg(names.size())
        : uiText("Attention required"));
    m_summary->setStyleSheet(busy() ? "background:#e4eef4;color:#37586d;" :
        m_report.passed() ? "background:#e3f0e7;color:#2f7548;" : "background:#f9e6e6;color:#a43838;");
    m_error->setText(uiText(m_report.baselineError.toUtf8().constData()));
    m_error->setVisible(!m_report.baselineError.isEmpty());
    const auto updatedAt = QDateTime::fromString(m_report.baseline.value("updatedAtUtc").toString(), Qt::ISODateWithMs);
    m_metadata->setText(uiText("Runtime directory: %1").arg(m_directory) + "\n" +
        uiText("Last verified: %1").arg(m_report.checkedAt.isValid()
            ? m_report.checkedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : "--") + "    |    " +
        uiText("Baseline updated: %1").arg(updatedAt.isValid()
            ? updatedAt.toLocalTime().toString("yyyy-MM-dd HH:mm:ss") : "--"));
    updateButtons();
}

void IntegrityPage::requestApproval()
{
    if (busy() || m_runActive || m_access != AdminAccess::Supervisor) return;
    QStringList files;
    for (const auto& index : m_files->selectionModel()->selectedRows()) files.push_back(m_report.files[index.row()].path);
    if (files.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName("integrityApprovalDialog");
    dialog.setWindowTitle(uiText("Authorize Baseline Update"));
    dialog.setMinimumWidth(420);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);
    auto* message = new QLabel(uiText("Accept the selected files as the new baseline?") + "\n\n" + files.join('\n'), &dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto* password = new QLineEdit(&dialog);
    password->setObjectName("integrityApprovalPassword");
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(uiText("Daily administrator password"));
    password->setMinimumHeight(34);
    layout->addWidget(password);
    auto* reason = new QLineEdit(&dialog);
    reason->setObjectName("integrityApprovalReason");
    reason->setPlaceholderText(uiText("Approval reason"));
    reason->setMaxLength(240);
    reason->setMinimumHeight(34);
    layout->addWidget(reason);
    auto* error = new QLabel(&dialog);
    error->setWordWrap(true);
    error->setStyleSheet("color:#a43838;");
    layout->addWidget(error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Cancel)->setText(uiText("Cancel"));
    auto* approve = buttons->addButton(uiText("Authorize"), QDialogButtonBox::AcceptRole);
    approve->setProperty("primary", true);
    approve->setObjectName("integrityConfirmApproval");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(approve, &QPushButton::clicked, &dialog, [&] {
        if (StartupSupport::adminAccessForPassword(password->text()) != AdminAccess::Supervisor) {
            error->setText(uiText("Daily administrator password is required."));
            password->clear();
            password->setFocus();
        } else if (reason->text().trimmed().isEmpty()) {
            error->setText(uiText("Select files and enter an approval reason."));
            reason->setFocus();
        } else dialog.accept();
    });
    password->setFocus();
    if (dialog.exec() == QDialog::Accepted) startJob(files, password->text(), reason->text());
}

} // namespace PicoATE::Ui
