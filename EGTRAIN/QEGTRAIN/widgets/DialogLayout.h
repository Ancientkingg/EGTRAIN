#pragma once

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QtGlobal>

namespace DialogLayout {

// Transfers body and actions to dialog. The caller owns button roles, connections,
// validation, default action and any changes to application state.
inline QScrollArea* install(QDialog& dialog, const QString& heading,
                            const QString& context, QWidget* body,
                            QDialogButtonBox* actions,
                            QRect availableGeometry = QRect())
{
    if (!availableGeometry.isValid()) {
        QScreen* screen = QGuiApplication::screenAt(dialog.mapToGlobal(QPoint(0, 0)));
        if (!screen)
            screen = QGuiApplication::primaryScreen();
        if (screen)
            availableGeometry = screen->availableGeometry();
    }

    dialog.setProperty("dialogPresentation", true);
    const int lineHeight = QFontMetrics(dialog.font()).height();
    const int margin = qMax(16, lineHeight);
    const int spacing = qMax(8, lineHeight / 2);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(spacing);

    auto* title = new QLabel(heading, &dialog);
    title->setObjectName(QStringLiteral("dialogHeading"));
    title->setWordWrap(true);
    title->setMinimumWidth(0);
    title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title->setTextFormat(Qt::PlainText);
    QFont titleFont = dialog.font();
    titleFont.setBold(true);
    if (titleFont.pointSizeF() > 0)
        titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
    else if (titleFont.pixelSize() > 0)
        titleFont.setPixelSize(qRound(titleFont.pixelSize() * 1.25));
    title->setFont(titleFont);
    layout->addWidget(title);

    auto* content = new QWidget;
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(spacing);
    if (!context.isEmpty()) {
        auto* subtitle = new QLabel(context, content);
        subtitle->setObjectName(QStringLiteral("dialogContext"));
        subtitle->setTextFormat(Qt::PlainText);
        subtitle->setWordWrap(true);
        subtitle->setMinimumWidth(0);
        subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        contentLayout->addWidget(subtitle);
    }
    contentLayout->addWidget(body);

    auto* scroll = new QScrollArea(&dialog);
    scroll->setObjectName(QStringLiteral("dialogBodyScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumSize(0, 0);
    scroll->setWidget(content);
    QObject::connect(qApp, &QApplication::focusChanged, scroll,
                     [scroll, content](QWidget*, QWidget* focused) {
        if (focused && content->isAncestorOf(focused))
            scroll->ensureWidgetVisible(focused);
    });
    layout->addWidget(scroll, 1);
    layout->addWidget(actions);

    // The scroll viewport, not its content, determines the dialog's minimum size.
    // Reserve room for window decorations and keep the footer outside the viewport.
    if (availableGeometry.isValid()) {
        const int maxWidth = qMax(1, availableGeometry.width() * 9 / 10);
        const int maxHeight = qMax(1, availableGeometry.height() * 4 / 5);
        dialog.setMaximumSize(maxWidth, maxHeight);
        dialog.resize(qMin(maxWidth, qMax(480, lineHeight * 32)),
                      qMin(maxHeight, qMax(layout->sizeHint().height(), lineHeight * 16)));
    }
    return scroll;
}

} // namespace DialogLayout
