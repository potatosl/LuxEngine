#include <LuxEditor/Ui/IconFactory.hpp>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <array>

namespace LuxEditor {
QIcon MakeEditorIcon(EditorIcon icon, QColor color) {
    QPixmap pixels(24, 24);
    pixels.fill(Qt::transparent);
    QPainter painter(&pixels);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    switch (icon) {
    case EditorIcon::Select: {
        QPainterPath arrow;
        arrow.moveTo(5, 3);
        arrow.lineTo(5, 19);
        arrow.lineTo(9, 15);
        arrow.lineTo(12, 21);
        arrow.lineTo(15, 19);
        arrow.lineTo(12, 13);
        arrow.lineTo(18, 13);
        arrow.closeSubpath();
        painter.drawPath(arrow);
        break;
    }
    case EditorIcon::Move:
        painter.drawLine(12, 2, 12, 22);
        painter.drawLine(2, 12, 22, 12);
        painter.drawLine(12, 2, 9, 5);
        painter.drawLine(12, 2, 15, 5);
        painter.drawLine(12, 22, 9, 19);
        painter.drawLine(12, 22, 15, 19);
        painter.drawLine(2, 12, 5, 9);
        painter.drawLine(2, 12, 5, 15);
        painter.drawLine(22, 12, 19, 9);
        painter.drawLine(22, 12, 19, 15);
        break;
    case EditorIcon::Rotate:
        painter.drawArc(QRectF(4, 4, 16, 16), 45 * 16, 290 * 16);
        painter.drawLine(18, 4, 20, 9);
        painter.drawLine(18, 4, 13, 5);
        break;
    case EditorIcon::Scale:
        painter.drawRect(QRectF(5, 9, 10, 10));
        painter.drawLine(13, 11, 20, 4);
        painter.drawLine(15, 4, 20, 4);
        painter.drawLine(20, 4, 20, 9);
        break;
    case EditorIcon::NewFile:
        painter.drawRect(QRectF(5, 3, 12, 17));
        painter.drawLine(11, 9, 11, 16);
        painter.drawLine(7.5, 12.5, 14.5, 12.5);
        break;
    case EditorIcon::Open:
    case EditorIcon::Folder:
        painter.drawLine(3, 19, 5, 8);
        painter.drawLine(5, 8, 10, 8);
        painter.drawLine(10, 8, 12, 10);
        painter.drawLine(12, 10, 21, 10);
        painter.drawLine(21, 10, 19, 19);
        painter.drawLine(19, 19, 3, 19);
        painter.drawLine(4, 8, 4, 6);
        painter.drawLine(4, 6, 11, 6);
        break;
    case EditorIcon::Save:
        painter.drawRect(QRectF(4, 3, 16, 18));
        painter.drawRect(QRectF(8, 3, 8, 6));
        painter.drawRect(QRectF(8, 14, 8, 7));
        break;
    case EditorIcon::Sprite:
        painter.drawRect(QRectF(4, 4, 16, 16));
        painter.drawEllipse(QPointF(9, 9), 1.5, 1.5);
        painter.drawLine(5, 18, 10, 13);
        painter.drawLine(10, 13, 13, 16);
        painter.drawLine(13, 16, 18, 10);
        break;
    case EditorIcon::Mesh:
        painter.drawLine(12, 2, 21, 7);
        painter.drawLine(21, 7, 21, 17);
        painter.drawLine(21, 17, 12, 22);
        painter.drawLine(12, 22, 3, 17);
        painter.drawLine(3, 17, 3, 7);
        painter.drawLine(3, 7, 12, 2);
        painter.drawLine(3, 7, 12, 12);
        painter.drawLine(21, 7, 12, 12);
        painter.drawLine(12, 12, 12, 22);
        break;
    case EditorIcon::Search:
        painter.drawEllipse(QRectF(4, 4, 12, 12));
        painter.drawLine(15, 15, 21, 21);
        break;
    case EditorIcon::Grid:
        for (int x : {4, 11, 18})
            for (int y : {4, 11, 18})
                painter.drawRect(QRectF(x, y, 3, 3));
        break;
    case EditorIcon::List:
        for (int y : {6, 12, 18}) {
            painter.drawRect(QRectF(4, y - 1, 2, 2));
            painter.drawLine(9, y, 20, y);
        }
        break;
    case EditorIcon::Focus:
        painter.drawEllipse(QRectF(5, 5, 14, 14));
        painter.drawLine(12, 2, 12, 8);
        painter.drawLine(12, 16, 12, 22);
        painter.drawLine(2, 12, 8, 12);
        painter.drawLine(16, 12, 22, 12);
        break;
    case EditorIcon::Console:
        painter.drawRect(QRectF(3, 4, 18, 16));
        painter.drawLine(6, 8, 10, 12);
        painter.drawLine(10, 12, 6, 16);
        painter.drawLine(12, 16, 18, 16);
        break;
    case EditorIcon::Delete:
        painter.drawLine(4, 7, 20, 7);
        painter.drawLine(9, 4, 15, 4);
        painter.drawLine(6, 8, 7, 20);
        painter.drawLine(18, 8, 17, 20);
        painter.drawLine(7, 20, 17, 20);
        painter.drawLine(10, 10, 10, 17);
        painter.drawLine(14, 10, 14, 17);
        break;
    case EditorIcon::Brush:
        painter.drawLine(5, 18, 16, 7);
        painter.drawLine(7, 20, 18, 9);
        painter.drawLine(16, 7, 19, 4);
        painter.drawLine(18, 9, 21, 6);
        painter.drawLine(4, 19, 7, 20);
        painter.drawEllipse(QRectF(3, 19, 4, 2));
        break;
    case EditorIcon::Eraser:
        painter.drawPolygon(QPolygonF{QPointF(4, 15), QPointF(13, 5), QPointF(21, 13),
                                      QPointF(13, 21), QPointF(4, 21)});
        painter.drawLine(9, 21, 17, 9);
        break;
    case EditorIcon::Play:
        painter.drawPolygon(QPolygonF{QPointF(6, 4), QPointF(20, 12), QPointF(6, 20)});
        break;
    case EditorIcon::Pause:
        painter.drawRect(QRectF(5, 4, 5, 16));
        painter.drawRect(QRectF(14, 4, 5, 16));
        break;
    case EditorIcon::Step:
        painter.drawPolygon(QPolygonF{QPointF(4, 4), QPointF(16, 12), QPointF(4, 20)});
        painter.drawLine(19, 4, 19, 20);
        break;
    case EditorIcon::Stop:
        painter.drawRect(QRectF(5, 5, 14, 14));
        break;
    case EditorIcon::Bug:
        painter.drawEllipse(QRectF(7, 7, 10, 12));
        painter.drawLine(12, 3, 12, 7);
        painter.drawLine(4, 8, 8, 10);
        painter.drawLine(4, 16, 8, 15);
        painter.drawLine(20, 8, 16, 10);
        painter.drawLine(20, 16, 16, 15);
        break;
    }
    return QIcon(pixels);
}
} // namespace LuxEditor
