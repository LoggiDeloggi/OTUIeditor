#ifndef OTUIBUTTON_H
#define OTUIBUTTON_H

#include "widget.h"

namespace OTUI {
    class Button : public Widget
    {
    public:
        Button();
        Button(QString widgetId, QString dataPath, QString imagePath);
        ~Button();

        bool supportsTextProperty() const override { return true; }
        QString textProperty() const override { return m_text; }
        void setTextProperty(const QString &text) override { m_text = text; }

    private:
        void draw(QPainter &painter);

    private:
        QString m_text = "Button";
    };
}

#endif // OTUIBUTTON_H
