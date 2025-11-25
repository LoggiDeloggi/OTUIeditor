#ifndef OTUILABEL_H
#define OTUILABEL_H

#include <QDebug>
#include <QApplication>
#include "widget.h"

namespace OTUI {
    class Label : public Widget
    {
    public:
        Label();
        Label(QString widgetId, QString dataPath, QString imagePath);
        ~Label();

        bool supportsTextProperty() const override { return true; }
        QString textProperty() const override { return m_text; }
        void setTextProperty(const QString &text) override { m_text = text; }

    private:
        QColor getColor() const { return m_color; }
        void setColor(const QColor &color) {
            m_color = color;
        }

    public:
        void draw(QPainter &painter);

    private:
        QString m_text = "Label";
        QColor m_color = QColor("#dfdfdf");
    };
}

#endif // OTUILABEL_H
