#include "image.h"

#include <utility>

namespace OTUI {

Image::Image() : Widget()
{
}

Image::Image(QString widgetId, QString dataPath, QString imagePath)
    : Widget(std::move(widgetId), std::move(dataPath), std::move(imagePath))
{
}

}
