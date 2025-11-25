#ifndef OTUIPARSER_H
#define OTUIPARSER_H

#include <memory>
#include <vector>

#include <QString>
#include <QStringList>

#include "widget.h"

namespace OTUI {
class Parser
{
public:
    using WidgetPtr = std::unique_ptr<Widget>;
    using WidgetList = std::vector<WidgetPtr>;

    Parser() = default;
    ~Parser() = default;

    bool loadFromFile(const QString& path,
                      WidgetList& outWidgets,
                      QString* error = nullptr,
                      const QString& dataPath = QString()) const;
    bool saveToFile(const QString& path, const WidgetList& widgets, QString* error = nullptr) const;
    bool instantiateStyle(const QString& path,
                          const QString& styleName,
                          WidgetList& outWidgets,
                          QString* error = nullptr,
                          const QString& dataPath = QString()) const;
    QStringList listStyles(const QString& path, QString* error = nullptr) const;

private:
    WidgetPtr createPlaceholderWidget(const QString& fileStem) const;
};
}

#endif // OTUIPARSER_H
