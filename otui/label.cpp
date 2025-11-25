#include "label.h"
#include "corewindow.h"

#include <algorithm>

OTUI::Label::Label() : Widget()
{

}

OTUI::Label::Label(QString widgetId, QString dataPath, QString imagePath) : Widget(widgetId, dataPath, imagePath)
{

}

OTUI::Label::~Label()
{

}

void OTUI::Label::draw(QPainter &painter)
{
    painter.save();
    painter.setPen(getColor());
    painter.setFont(getFont());
    const int originX = x() + getParent()->x() + textOffset().x();
    const int originY = y() + getParent()->y() + textOffset().y();
    const int drawWidth = std::max(1, width() - textOffset().x());
    const int drawHeight = std::max(1, height() - textOffset().y());
    int flags = static_cast<int>(textAlignment());
    if(textWrap())
        flags |= Qt::TextWordWrap;
    painter.drawText(originX, originY, drawWidth, drawHeight, flags, textProperty());
    painter.restore();
}
