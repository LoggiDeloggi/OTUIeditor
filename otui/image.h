#ifndef OTUIIMAGE_H
#define OTUIIMAGE_H

#include "widget.h"

namespace OTUI {
class Image : public Widget
{
public:
    Image();
    Image(QString widgetId, QString dataPath, QString imagePath);
    ~Image() override = default;
};
}

#endif // OTUIIMAGE_H
