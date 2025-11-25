#include "button.h"

#include <algorithm>

OTUI::Button::Button() : Widget()
{
    m_parent = nullptr;
    setTextAlignment(Qt::AlignCenter);
}

OTUI::Button::Button(QString widgetId, QString dataPath, QString imagePath) : Widget(widgetId, dataPath, imagePath)
{
    m_parent = nullptr;
    setTextAlignment(Qt::AlignCenter);
}

OTUI::Button::~Button()
{

}

void OTUI::Button::draw(QPainter &painter)
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
