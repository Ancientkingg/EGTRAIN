#include "widgets/DialogLayout.h"

#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>

#include <iostream>

namespace {
bool check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

bool exercise(const QRect& screen, qreal scale)
{
    QDialog dialog;
    QFont font = dialog.font();
    font.setPointSizeF(12 * scale);
    dialog.setFont(font);
    auto* body = new QWidget;
    auto* fields = new QVBoxLayout(body);
    for (int i = 0; i < 80; ++i) {
        auto* label = new QLabel(QStringLiteral("Service %1: Copenhagen to a long destination with a long platform name")
                                     .arg(i), body);
        label->setWordWrap(true);
        fields->addWidget(label);
    }
    auto* lastField = new QLineEdit(body);
    lastField->setPlaceholderText(QStringLiteral("Last field"));
    fields->addWidget(lastField);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    buttons->button(QDialogButtonBox::Ok)->setDefault(true);
    const QString longName(2500, QLatin1Char('X'));
    auto* scroll = DialogLayout::install(dialog, QStringLiteral("Review <simulation> & " ) + longName.left(120),
                                         QStringLiteral("Case <identifier>: ") + longName,
                                         body, buttons, screen);
    dialog.show();
    QApplication::processEvents();
    bool ok = check(dialog.width() <= screen.width() * 9 / 10 &&
                        dialog.height() <= screen.height() * 4 / 5,
                    "dialog exceeds available screen budget");
    ok &= check(scroll->verticalScrollBar()->maximum() > 0,
                "long content does not scroll");
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    ok &= check(buttons->isVisible() && buttons->geometry().bottom() <= dialog.height() &&
                    scroll->geometry().bottom() <= buttons->geometry().top(),
                "footer must remain reachable outside the scrolling body");
    auto* heading = dialog.findChild<QLabel*>(QStringLiteral("dialogHeading"));
    auto* context = dialog.findChild<QLabel*>(QStringLiteral("dialogContext"));
    ok &= check(heading && heading->font().pointSizeF() > font.pointSizeF() &&
                    heading->textFormat() == Qt::PlainText && context &&
                    context->textFormat() == Qt::PlainText &&
                    context->parentWidget() == scroll->widget(),
                "heading scales, authored labels remain plain and context scrolls");
    scroll->ensureWidgetVisible(lastField);
    lastField->setFocus();
    QKeyEvent tab(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QApplication::sendEvent(lastField, &tab);
    ok &= check(buttons->button(QDialogButtonBox::Ok)->hasFocus() ||
                    buttons->button(QDialogButtonBox::Cancel)->hasFocus(),
                "Tab from last body field reaches fixed footer");
    buttons->button(QDialogButtonBox::Ok)->setFocus();
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(buttons->button(QDialogButtonBox::Ok), &enter);
    ok &= check(dialog.result() == QDialog::Accepted, "default Enter must accept");

    QDialog cancelDialog;
    auto* cancelBody = new QWidget;
    cancelBody->setLayout(new QVBoxLayout);
    auto* cancelButtons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    QObject::connect(cancelButtons, &QDialogButtonBox::accepted, &cancelDialog, &QDialog::accept);
    QObject::connect(cancelButtons, &QDialogButtonBox::rejected, &cancelDialog, &QDialog::reject);
    DialogLayout::install(cancelDialog, QStringLiteral("Choose case"), QString(),
                          cancelBody, cancelButtons, screen);
    cancelDialog.show();
    QApplication::processEvents();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&cancelDialog, &escape);
    ok &= check(cancelDialog.result() == QDialog::Rejected && !cancelDialog.isVisible(),
                "Escape must reject without accepting");
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QFile qss(QStringLiteral(EGTRAIN_DIALOG_QSS));
    if (!qss.open(QIODevice::ReadOnly)) {
        std::cerr << "cannot load application QSS\n";
        return 1;
    }
    app.setStyleSheet(QString::fromUtf8(qss.readAll()));
    const bool small = exercise(QRect(0, 0, 1280, 800), 1.0);
    const bool scaledSmall = exercise(QRect(0, 0, 1280, 800), 1.5);
    const bool large = exercise(QRect(0, 0, 1920, 1080), 1.5);
    return small && scaledSmall && large ? 0 : 1;
}
