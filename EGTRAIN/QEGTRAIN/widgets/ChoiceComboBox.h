#pragma once

#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QComboBox>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QScreen>
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QtGlobal>

// Combo box for choice fields in dialogs whose entries can be long imported names.
// The field can shrink below its text so that a form never needs a horizontal scroll
// bar, while its size hint still lets the dialog grow to show the whole text. Its tooltip
// is the current text, and the popup list is as wide as its widest entry within the screen.
class ChoiceComboBox : public QComboBox {
public:
    explicit ChoiceComboBox(QWidget* parent = nullptr)
        : QComboBox(parent)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setSizeAdjustPolicy(AdjustToContents);
        view()->setTextElideMode(Qt::ElideMiddle);
        const auto sync = [this]() { setToolTip(currentText()); };
        connect(this, &QComboBox::currentTextChanged, this, sync);
        connect(this, qOverload<int>(&QComboBox::currentIndexChanged), this, sync);
        connect(model(), &QAbstractItemModel::rowsInserted, this, sync);
        connect(model(), &QAbstractItemModel::rowsRemoved, this, sync);
        connect(model(), &QAbstractItemModel::modelReset, this, sync);
        connect(model(), &QAbstractItemModel::dataChanged, this, sync);
    }

    QSize minimumSizeHint() const override
    {
        const QSize hint = QComboBox::sizeHint();
        QStyleOptionComboBox option;
        initStyleOption(&option);
        const QFontMetrics metrics(font());
        const QSize contents(metrics.horizontalAdvance(QLatin1Char('x')) * 12, metrics.height());
        const int width = style()->sizeFromContents(QStyle::CT_ComboBox, &option, contents, this).width();
        return QSize(qMin(width, hint.width()), hint.height());
    }

    void showPopup() override
    {
        QScreen* target = QGuiApplication::screenAt(mapToGlobal(rect().center()));
        if (!target)
            target = QGuiApplication::primaryScreen();
        const QRect available = target ? target->availableGeometry() : QRect();
        const int budget = available.isValid() ? available.width() * 9 / 10 : QWIDGETSIZE_MAX;
        QAbstractItemView* list = view();
        list->setMinimumWidth(qMin(budget, qMax(width(), list->sizeHintForColumn(0)
            + 2 * list->frameWidth() + list->verticalScrollBar()->sizeHint().width())));
        QComboBox::showPopup();

        // The popup is as wide as the combo by default; keep it within the budget and the screen.
        QWidget* popup = list->window();
        QRect geometry = popup->geometry();
        geometry.setWidth(qMin(geometry.width(), budget + popup->width() - list->width()));
        if (available.isValid()) {
            if (geometry.right() > available.right())
                geometry.moveRight(available.right());
            if (geometry.left() < available.left())
                geometry.moveLeft(available.left());
        }
        popup->setGeometry(geometry);
    }

protected:
    bool event(QEvent* event) override
    {
        // Items can change while signals are blocked, so refresh when the tip is shown.
        if (event->type() == QEvent::ToolTip)
            setToolTip(currentText());
        return QComboBox::event(event);
    }
};
