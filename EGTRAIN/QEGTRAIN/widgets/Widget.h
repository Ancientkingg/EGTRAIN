#ifndef WIDGET_H
#define WIDGET_H

#include <iostream>
#include <QMainWindow>
#include <QPainter>
#include <QColor>
#include <QBrush>
#include <QPixmap>
#include <QtGui>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QPen>
#include <array>
#include <QGraphicsEllipseItem>
#include <QPoint>
#include <QSizePolicy>
#include <QLineF>
#include <QMessageBox>
#include <QWheelEvent>
#include <QScrollBar>
#include <QMouseEvent>
#include <QObject>
#include <qmath.h>
#include <QApplication>
#include <QImage>
#include <string>
#include <QGraphicsTextItem>
#include <QLabel>
#include <QTextOption>
#include <QGraphicsSceneMouseEvent>
#include <QMenu>
#include <QTransform>
#include <cmath>
#include <QDockWidget>
#include <Qt>
#include <QLineEdit>
#include <sstream>
#include <QGraphicsPixmapItem>
#include <QGraphicsItemGroup>
#include <QList>
#include <QFont>
#include <QFormLayout>
#include <QGraphicsEffect>
#include <QGraphicsColorizeEffect>
#include <QTimer>
#include <QTime>
#include <algorithm>
#include <limits>
#include <QVBoxLayout>
#include <list>
#include <vector>
#include <map>
#include <tuple>
#include <QStatusBar>
#include <QFileDialog>

namespace Ui {
class Widget;
}
class Widget : public QWidget {
	Q_OBJECT
public:
	explicit Widget(QWidget* parent = nullptr);
	~Widget();

private:
	Ui::Widget* ui;
};

#endif
