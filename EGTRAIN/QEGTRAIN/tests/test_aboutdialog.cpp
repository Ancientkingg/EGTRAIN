#include "widgets/AboutDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUrl>

#include <iostream>

class UrlReceiver : public QObject {
    Q_OBJECT
public:
    QList<QUrl> opened;
public slots:
    void receive(const QUrl& url) { opened.append(url); }
};

static bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationVersion(QStringLiteral("9.8.7-test"));
    UrlReceiver receiver;
    QDesktopServices::setUrlHandler(QStringLiteral("https"), &receiver, "receive");

    bool ok = true;
    const char* names[] = {"aboutResearchProfile", "aboutOriginalResearch", "aboutWebsite",
                           "aboutGitHub", "aboutSourceCode", "aboutReportIssue"};
    const char* addresses[] = {"https://orcid.org/0000-0002-7936-5832",
                               "https://doi.org/10.6092/unina/fedoa/8599",
                               "https://samuelbruin.com/", "https://github.com/Ancientkingg",
                               "https://github.com/Ancientkingg/EGTRAIN",
                               "https://github.com/Ancientkingg/EGTRAIN/issues"};
    for (qreal scale : {1.0, 1.5}) {
        AboutDialog dialog;
        QFont font = dialog.font();
        font.setPointSizeF(12 * scale);
        dialog.setFont(font);
        dialog.show();
        app.processEvents();
        ok &= check(dialog.windowTitle() == QStringLiteral("About EGTRAIN"), "wrong dialog title");
        ok &= check(receiver.opened.isEmpty(), "opening About launched a browser");
        const auto labels = dialog.findChildren<QLabel*>();
        QString text;
        for (const auto* label : labels)
            text += label->text() + QLatin1Char('\n');
        ok &= check(text.contains(QStringLiteral("9.8.7-test")) &&
                        text.contains(QStringLiteral("Prof. Egidio Quaglietta")) &&
                        text.contains(QStringLiteral("Original EGTRAIN model and research.")) &&
                        text.contains(QStringLiteral("Samuel Bruin")) &&
                        text.contains(QStringLiteral("Continued desktop application development and maintenance.")) &&
                        text.contains(QStringLiteral("Microscopic railway simulation for students and researchers.")),
                    "About content or application version missing");
        auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
        ok &= check(context && (context->textInteractionFlags() & Qt::TextSelectableByKeyboard),
                    "version and description must be selectable");
        auto* scroll = dialog.findChild<QScrollArea*>(QStringLiteral("dialogBodyScroll"));
        auto* footer = dialog.findChild<QDialogButtonBox*>();
        ok &= check(dialog.width() <= 1152 && dialog.height() <= 640 && scroll && footer &&
                        footer->isVisible() && footer->geometry().top() >= scroll->geometry().bottom(),
                    "bounded dialog must retain its Close footer");
        for (int i = 0; i < 6; ++i) {
            auto* link = dialog.findChild<QPushButton*>(QString::fromLatin1(names[i]));
            if (!check(link && link->toolTip() == QString::fromLatin1(addresses[i]) &&
                           link->accessibleDescription() == link->toolTip(), "wrong link address")) {
                ok = false;
                continue;
            }
            link->setFocus();
            QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            app.sendEvent(link, &enter);
            ok &= check(receiver.opened.size() == i + 1 &&
                            receiver.opened.last() == QUrl(QString::fromLatin1(addresses[i])) &&
                            dialog.isVisible(), "keyboard activation must open only selected link");
        }
        receiver.opened.clear();
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        app.sendEvent(&dialog, &escape);
        ok &= check(!dialog.isVisible() && dialog.result() == QDialog::Rejected,
                    "Escape must reject About");
        dialog.show();
        footer->button(QDialogButtonBox::Close)->click();
        ok &= check(!dialog.isVisible(), "Close button must dismiss About");
    }
    QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
    // The offscreen platform has no browser launcher. Never exercise the real
    // desktop handler when this test is run with a native platform plugin.
    if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
        AboutDialog dialog;
        dialog.show();
        dialog.findChild<QPushButton*>(QStringLiteral("aboutWebsite"))->click();
        auto* status = dialog.findChild<QLabel*>(QStringLiteral("aboutLinkStatus"));
        ok &= check(dialog.isVisible() && status && status->isVisible() &&
                        status->text().contains(QStringLiteral("https://samuelbruin.com/")),
                    "browser failure must preserve About and show the address");
        dialog.close();
    }
    return ok ? 0 : 1;
}

#include "test_aboutdialog.moc"
