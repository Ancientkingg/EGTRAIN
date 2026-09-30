#include "widgets/AboutDialog.h"
#include "widgets/DialogLayout.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("aboutDialog"));
    setWindowTitle(tr("About EGTRAIN"));

    auto* body = new QWidget;
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(qMax(12, fontMetrics().height()));

    const QIcon icon = parent ? parent->windowIcon() : QApplication::windowIcon();
    if (!icon.isNull()) {
        auto* image = new QLabel(body);
        const int iconSize = qMax(48, fontMetrics().height() * 3);
        image->setPixmap(icon.pixmap(iconSize, iconSize));
        image->setAccessibleName(tr("EGTRAIN application icon"));
        column->addWidget(image, 0, Qt::AlignLeft);
    }

    auto* failure = new QLabel(body);
    failure->setObjectName(QStringLiteral("aboutLinkStatus"));
    failure->setProperty("dialogStatus", "warning");
    failure->setWordWrap(true);
    failure->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    failure->hide();

    auto addText = [body](QVBoxLayout* layout, const QString& text, bool bold = false) {
        auto* label = new QLabel(text, body);
        label->setTextFormat(Qt::PlainText);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        label->setWordWrap(true);
        if (bold) {
            QFont nameFont = label->font();
            nameFont.setBold(true);
            label->setFont(nameFont);
        }
        layout->addWidget(label);
    };
    auto addSection = [body, column, &addText](const QString& name, const QString& role) {
        auto* section = new QVBoxLayout;
        section->setSpacing(qMax(4, body->fontMetrics().height() / 4));
        addText(section, name, true);
        addText(section, role);
        column->addLayout(section);
    };
    auto addLink = [this, body, column, failure](const QString& label, const char* address,
                                                  const char* objectName) {
        const QUrl url(QString::fromLatin1(address));
        auto* button = new QPushButton(label, body);
        button->setObjectName(QString::fromLatin1(objectName));
        button->setProperty("aboutLink", true);
        button->setFlat(true);
        button->setAutoDefault(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setToolTip(url.toString());
        button->setAccessibleDescription(url.toString());
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
        connect(button, &QPushButton::clicked, this, [failure, url]() {
            if (!QDesktopServices::openUrl(url)) {
                failure->setText(QCoreApplication::translate("AboutDialog", "Could not open the link in your browser. Address: %1").arg(url.toString()));
                failure->show();
            } else {
                failure->hide();
            }
        });
        column->addWidget(button, 0, Qt::AlignLeft);
        return button;
    };

    addSection(tr("Prof. Egidio Quaglietta"), tr("Original EGTRAIN model and research."));
    auto* firstLink = addLink(tr("Research profile (ORCID)"),
                              "https://orcid.org/0000-0002-7936-5832", "aboutResearchProfile");
    addLink(tr("Original research (doctoral thesis)"),
            "https://doi.org/10.6092/unina/fedoa/8599", "aboutOriginalResearch");

    addSection(tr("Samuel Bruin"), tr("Continued desktop application development and maintenance."));
    addLink(tr("Website"), "https://samuelbruin.com/", "aboutWebsite");
    addLink(tr("GitHub"), "https://github.com/Ancientkingg", "aboutGitHub");

    addText(column, tr("Project links"), true);
    addLink(tr("Source code"), "https://github.com/Ancientkingg/EGTRAIN", "aboutSourceCode");
    addLink(tr("Report an issue"), "https://github.com/Ancientkingg/EGTRAIN/issues", "aboutReportIssue");
    column->addWidget(failure);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttons->button(QDialogButtonBox::Close)->setDefault(true);
    DialogLayout::install(*this, tr("EGTRAIN"),
                          tr("Version %1\nMicroscopic railway simulation for students and researchers.")
                              .arg(QCoreApplication::applicationVersion()),
                          body, buttons);
    if (auto* context = findChild<QLabel*>(QStringLiteral("dialogContext")))
        context->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    firstLink->setFocus();
}
