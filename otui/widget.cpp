#include "widget.h"
#include "corewindow.h"

#include <QPixmapCache>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <functional>
#include <QStringList>

namespace {

QString normalizeRootPath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(path.trimmed());
    while(normalized.endsWith('/'))
        normalized.chop(1);
    return normalized;
}

QString g_modulesRootPath;
QString g_moduleAssetsRoot;

}

namespace OTUI {

void setModulesRootPath(const QString &path)
{
    g_modulesRootPath = normalizeRootPath(path);
}

void setModuleAssetsRoot(const QString &path)
{
    g_moduleAssetsRoot = normalizeRootPath(path);
}

}

OTUI::Widget::Widget() : m_id("widgetid")
{
    m_parent = nullptr;
    m_font = QFont("Verdana", 11);
    m_color = QColor(223, 223, 223);
    m_opacity = 1.0f;
}

OTUI::Widget::Widget(QString widgetId)
{
    m_parent = nullptr;
    m_id.clear();
    m_id.append(widgetId);
    m_rect = QRect(0, 0, 32, 32);
    m_imageCrop.setRect(0, 0, -1, -1);
    m_font = QFont("Verdana", 11);
    m_color = QColor(223, 223, 223);
    m_opacity = 1.0f;
}

OTUI::Widget::Widget(QString widgetId, QString dataPath)
{
    Q_UNUSED(dataPath);
    m_parent = nullptr;
    m_id = widgetId;
    m_imageSource.clear();
    m_image = QPixmap();
    m_rect = QRect(0, 0, 32, 32);
    m_imageCrop.setRect(0, 0, -1, -1);
    m_imageSize = QPoint(m_rect.width(), m_rect.height());
    m_font = QFont("Verdana", 11);
    m_color = QColor(223, 223, 223);
    m_opacity = 1.0f;
}

OTUI::Widget::Widget(QString widgetId, QString dataPath, QString imagePath)
{
    m_parent = nullptr;
    m_id.clear();
    m_id.append(widgetId);
    m_imageSource = imagePath;
    QString fullImagePath(dataPath + "/" + imagePath);
    if(!fullImagePath.isEmpty())
    {
        if (!QPixmapCache::find(fullImagePath, &m_image)) {
            m_image.load(fullImagePath);
            QPixmapCache::insert(fullImagePath, m_image);
        }
    }
    m_imageSize = QPoint(m_image.width(), m_image.height());
    m_rect = QRect(0, 0, m_image.width(), m_image.height());
    m_imageCrop.setRect(0, 0, m_image.width(), m_image.height());
    m_font = QFont("Verdana", 11);
    m_color = QColor(223, 223, 223);
    m_opacity = 1.0f;
}

void OTUI::Widget::event(QEvent *event)
{
    if(event->type() == SettingsSavedEvent::eventType)
    {
        SettingsSavedEvent *settings = reinterpret_cast<SettingsSavedEvent*>(event);
        setImageSource(m_imageSource, settings->dataPath);
    }
}

void OTUI::Widget::setId(const QString id) {
    m_id = id;
}

void OTUI::Widget::setIdProperty(const QString &id) {
    if(m_id == nullptr || id.size() == 0) return;

    SetIdEvent *event = new SetIdEvent();
    event->oldId = m_id;
    event->newId = id;
    m_id = id;
    qApp->postEvent(qApp->activeWindow(), event);
}

void OTUI::Widget::setPosition(const QVector2D &position)
{
    setPos(QPoint(position.x(), position.y()));
}

void OTUI::Widget::setSizeProperty(const QPoint &size)
{
    m_rect.setWidth(size.x());
    m_rect.setHeight(size.y());
    if(getParent() != nullptr)
    {
        if(getRect()->right() > getParent()->width())
            getRect()->setRight(getParent()->width());
        if(getRect()->bottom() > getParent()->height())
            getRect()->setBottom(getParent()->height());
    }
}

void OTUI::Widget::setOpacity(float opacity)
{
    m_opacity = std::clamp(opacity, 0.0f, 1.0f);
}

bool OTUI::Widget::supportsTextProperty() const
{
    return false;
}

QString OTUI::Widget::textProperty() const
{
    return QString();
}

void OTUI::Widget::setTextProperty(const QString &text)
{
    Q_UNUSED(text);
}

void OTUI::Widget::clearAnchors()
{
    m_anchorLeft = AnchorBinding();
    m_anchorRight = AnchorBinding();
    m_anchorTop = AnchorBinding();
    m_anchorBottom = AnchorBinding();
    m_anchorHorizontalCenter = AnchorBinding();
    m_anchorVerticalCenter = AnchorBinding();
}

void OTUI::Widget::setAnchorBinding(AnchorEdge edge, const QString &targetId, AnchorEdge targetEdge)
{
    AnchorBinding binding;
    binding.edge = targetEdge;
    binding.targetId = targetId;

    switch(edge)
    {
    case AnchorEdge::Left:
        m_anchorLeft = binding;
        break;
    case AnchorEdge::Right:
        m_anchorRight = binding;
        break;
    case AnchorEdge::Top:
        m_anchorTop = binding;
        break;
    case AnchorEdge::Bottom:
        m_anchorBottom = binding;
        break;
    case AnchorEdge::HorizontalCenter:
        m_anchorHorizontalCenter = binding;
        break;
    case AnchorEdge::VerticalCenter:
        m_anchorVerticalCenter = binding;
        break;
    default:
        break;
    }
}

void OTUI::Widget::clearAnchorBinding(AnchorEdge edge)
{
    setAnchorBinding(edge, QString(), AnchorEdge::None);
}

namespace {
QString anchorEdgeName(OTUI::AnchorEdge edge)
{
    switch(edge)
    {
    case OTUI::AnchorEdge::Left:
        return QStringLiteral("left");
    case OTUI::AnchorEdge::Right:
        return QStringLiteral("right");
    case OTUI::AnchorEdge::Top:
        return QStringLiteral("top");
    case OTUI::AnchorEdge::Bottom:
        return QStringLiteral("bottom");
    case OTUI::AnchorEdge::HorizontalCenter:
        return QStringLiteral("horizontalCenter");
    case OTUI::AnchorEdge::VerticalCenter:
        return QStringLiteral("verticalCenter");
    case OTUI::AnchorEdge::None:
    default:
        return QString();
    }
}

OTUI::AnchorEdge parseAnchorEdgeToken(const QString &token)
{
    const QString lowered = token.trimmed().toLower();
    if(lowered == QStringLiteral("left"))
        return OTUI::AnchorEdge::Left;
    if(lowered == QStringLiteral("right"))
        return OTUI::AnchorEdge::Right;
    if(lowered == QStringLiteral("top"))
        return OTUI::AnchorEdge::Top;
    if(lowered == QStringLiteral("bottom"))
        return OTUI::AnchorEdge::Bottom;
    if(lowered == QStringLiteral("horizontalcenter") || lowered == QStringLiteral("centerx"))
        return OTUI::AnchorEdge::HorizontalCenter;
    if(lowered == QStringLiteral("verticalcenter") || lowered == QStringLiteral("centery"))
        return OTUI::AnchorEdge::VerticalCenter;
    return OTUI::AnchorEdge::None;
}
}

OTUI::AnchorBinding OTUI::Widget::anchorBinding(AnchorEdge edge) const
{
    switch(edge)
    {
    case AnchorEdge::Left:
        return m_anchorLeft;
    case AnchorEdge::Right:
        return m_anchorRight;
    case AnchorEdge::Top:
        return m_anchorTop;
    case AnchorEdge::Bottom:
        return m_anchorBottom;
    case AnchorEdge::HorizontalCenter:
        return m_anchorHorizontalCenter;
    case AnchorEdge::VerticalCenter:
        return m_anchorVerticalCenter;
    case AnchorEdge::None:
    default:
        return AnchorBinding();
    }
}

QString OTUI::Widget::anchorDescriptor(AnchorEdge edge) const
{
    const AnchorBinding binding = anchorBinding(edge);
    if(!binding.isValid())
        return QString();
    const QString token = anchorEdgeName(binding.edge);
    if(token.isEmpty())
        return QString();
    return QStringLiteral("%1.%2").arg(binding.targetId, token);
}

bool OTUI::Widget::setAnchorFromDescriptor(AnchorEdge edge, const QString &descriptor)
{
    const QString trimmed = descriptor.trimmed();
    if(trimmed.isEmpty())
    {
        clearAnchorBinding(edge);
        return true;
    }

    const QStringList parts = trimmed.split('.', Qt::SkipEmptyParts);
    if(parts.size() != 2)
        return false;

    const AnchorEdge targetEdge = parseAnchorEdgeToken(parts.at(1));
    if(targetEdge == AnchorEdge::None)
        return false;

    setAnchorBinding(edge, parts.at(0).trimmed(), targetEdge);
    return true;
}

QString OTUI::Widget::centerInTarget() const
{
    if(!m_anchorHorizontalCenter.isValid() || !m_anchorVerticalCenter.isValid())
        return QString();
    if(m_anchorHorizontalCenter.edge != AnchorEdge::HorizontalCenter)
        return QString();
    if(m_anchorVerticalCenter.edge != AnchorEdge::VerticalCenter)
        return QString();
    if(m_anchorHorizontalCenter.targetId != m_anchorVerticalCenter.targetId)
        return QString();
    return m_anchorHorizontalCenter.targetId;
}

void OTUI::Widget::setCenterInTarget(const QString &targetId)
{
    const QString trimmed = targetId.trimmed();
    if(trimmed.isEmpty())
    {
        clearAnchorBinding(AnchorEdge::HorizontalCenter);
        clearAnchorBinding(AnchorEdge::VerticalCenter);
        return;
    }

    setAnchorBinding(AnchorEdge::HorizontalCenter, trimmed, AnchorEdge::HorizontalCenter);
    setAnchorBinding(AnchorEdge::VerticalCenter, trimmed, AnchorEdge::VerticalCenter);
}

QString OTUI::Widget::fillTarget() const
{
    if(!m_anchorLeft.isValid() || !m_anchorRight.isValid() ||
       !m_anchorTop.isValid() || !m_anchorBottom.isValid())
        return QString();

    if(m_anchorLeft.edge != AnchorEdge::Left ||
       m_anchorRight.edge != AnchorEdge::Right ||
        m_anchorTop.edge != AnchorEdge::Top ||
        m_anchorBottom.edge != AnchorEdge::Bottom)
        return QString();

    if(m_anchorLeft.targetId != m_anchorRight.targetId ||
       m_anchorLeft.targetId != m_anchorTop.targetId ||
       m_anchorLeft.targetId != m_anchorBottom.targetId)
        return QString();

    return m_anchorLeft.targetId;
}

void OTUI::Widget::setFillTarget(const QString &targetId)
{
    const QString trimmed = targetId.trimmed();
    if(trimmed.isEmpty())
    {
        clearAnchorBinding(AnchorEdge::Left);
        clearAnchorBinding(AnchorEdge::Right);
        clearAnchorBinding(AnchorEdge::Top);
        clearAnchorBinding(AnchorEdge::Bottom);
        return;
    }

    setAnchorBinding(AnchorEdge::Left, trimmed, AnchorEdge::Left);
    setAnchorBinding(AnchorEdge::Right, trimmed, AnchorEdge::Right);
    setAnchorBinding(AnchorEdge::Top, trimmed, AnchorEdge::Top);
    setAnchorBinding(AnchorEdge::Bottom, trimmed, AnchorEdge::Bottom);
}

namespace {
QPoint absolutePosition(const OTUI::Widget *widget)
{
    if(!widget)
        return QPoint();
    QPoint pos = widget->getPos();
    const OTUI::Widget *parent = widget->getParent();
    while(parent)
    {
        pos += parent->getPos();
        parent = parent->getParent();
    }
    return pos;
}

int edgeCoordinate(const OTUI::Widget *widget, OTUI::AnchorEdge edge)
{
    if(!widget)
        return 0;
    QPoint abs = absolutePosition(widget);
    switch(edge)
    {
    case OTUI::AnchorEdge::Left:
        return abs.x();
    case OTUI::AnchorEdge::Right:
        return abs.x() + widget->width();
    case OTUI::AnchorEdge::Top:
        return abs.y();
    case OTUI::AnchorEdge::Bottom:
        return abs.y() + widget->height();
    case OTUI::AnchorEdge::HorizontalCenter:
        return abs.x() + widget->width() / 2;
    case OTUI::AnchorEdge::VerticalCenter:
        return abs.y() + widget->height() / 2;
    default:
        return 0;
    }
}
}

void OTUI::Widget::applyAnchors(const std::function<Widget*(const QString &)> &resolver)
{
    auto resolveTarget = [&](const AnchorBinding &binding) -> Widget* {
        if(!binding.isValid())
            return nullptr;
        if(binding.targetId.compare("parent", Qt::CaseInsensitive) == 0)
            return m_parent;
        if(!resolver)
            return nullptr;
        return resolver(binding.targetId);
    };

    QPoint parentAbs = absolutePosition(m_parent);
    QRect rect = *getRect();

    auto applyHorizontal = [&](bool forRight) {
        const AnchorBinding &binding = forRight ? m_anchorRight : m_anchorLeft;
        Widget *target = resolveTarget(binding);
        if(!target)
            return;
        const int targetCoord = edgeCoordinate(target, binding.edge) - parentAbs.x();
        if(forRight)
        {
            const int rightPos = targetCoord - m_margin.right;
            if(binding.isValid())
            {
                if(m_anchorLeft.isValid())
                {
                    const int newWidth = std::max(1, rightPos - rect.left());
                    rect.setWidth(newWidth);
                }
                else
                {
                    rect.moveLeft(rightPos - rect.width());
                }
            }
        }
        else
        {
            rect.moveLeft(targetCoord + m_margin.left);
        }
    };

    auto applyVertical = [&](bool forBottom) {
        const AnchorBinding &binding = forBottom ? m_anchorBottom : m_anchorTop;
        Widget *target = resolveTarget(binding);
        if(!target)
            return;
        const int targetCoord = edgeCoordinate(target, binding.edge) - parentAbs.y();
        if(forBottom)
        {
            const int bottomPos = targetCoord - m_margin.bottom;
            if(binding.isValid())
            {
                if(m_anchorTop.isValid())
                {
                    rect.setHeight(std::max(1, bottomPos - rect.top()));
                }
                else
                {
                    rect.moveTop(bottomPos - rect.height());
                }
            }
        }
        else
        {
            rect.moveTop(targetCoord + m_margin.top);
        }
    };

    auto applyCenter = [&](bool horizontal) {
        const AnchorBinding &binding = horizontal ? m_anchorHorizontalCenter : m_anchorVerticalCenter;
        Widget *target = resolveTarget(binding);
        if(!target)
            return;
        const int targetCoord = edgeCoordinate(target, binding.edge);
        if(horizontal)
        {
            const int localCenter = targetCoord - parentAbs.x();
            rect.moveLeft(localCenter - rect.width() / 2);
        }
        else
        {
            const int localCenter = targetCoord - parentAbs.y();
            rect.moveTop(localCenter - rect.height() / 2);
        }
    };

    applyHorizontal(false);
    applyHorizontal(true);
    applyVertical(false);
    applyVertical(true);
    applyCenter(true);
    applyCenter(false);

    setRect(rect);
}

QString OTUI::Widget::colorString() const
{
    if(!m_color.isValid())
        return QString();
    return m_color.name(QColor::HexArgb);
}

void OTUI::Widget::setImageSource(const QString &source, const QString &dataPath)
{
    QString normalized = QDir::fromNativeSeparators(source);
    normalized = normalized.trimmed();
    if(normalized.isEmpty())
    {
        m_imageSource.clear();
        m_image = QPixmap();
        return;
    }

    if(!normalized.startsWith('/'))
        normalized.prepend('/');

    m_imageSource = normalized;

    QStringList searchRoots;
    static const QStringList kFallbackExtensions = {
        QStringLiteral(".png"),
        QStringLiteral(".jpg"),
        QStringLiteral(".jpeg"),
        QStringLiteral(".bmp"),
        QStringLiteral(".dds")
    };

    const int lastDot = normalized.lastIndexOf('.');
    const int lastSlash = normalized.lastIndexOf('/');
    const bool needsExtensionGuess = (lastDot <= lastSlash);

    if(!dataPath.isEmpty())
    {
        QString normalizedRoot = QDir::fromNativeSeparators(dataPath);
        if(normalizedRoot.endsWith('/'))
            normalizedRoot.chop(1);
        if(!normalizedRoot.isEmpty())
            searchRoots << normalizedRoot;

        if(normalized.startsWith(QStringLiteral("/modules/")))
        {
            QDir parent(normalizedRoot);
            if(parent.cdUp())
            {
                const QString parentPath = parent.absolutePath();
                if(!parentPath.isEmpty() && !searchRoots.contains(parentPath))
                    searchRoots << parentPath;
            }
        }
    }

    if(normalized.startsWith(QStringLiteral("/modules/")) && !g_modulesRootPath.isEmpty())
    {
        if(!searchRoots.contains(g_modulesRootPath))
            searchRoots << g_modulesRootPath;
    }

    if(!g_moduleAssetsRoot.isEmpty())
    {
        if(!searchRoots.contains(g_moduleAssetsRoot))
            searchRoots << g_moduleAssetsRoot;
    }

    const auto tryLoad = [&](const QString &path) {
        if(path.isEmpty())
            return false;
        if(QPixmapCache::find(path, &m_image))
            return !m_image.isNull();
        if(QFileInfo::exists(path) && m_image.load(path))
        {
            QPixmapCache::insert(path, m_image);
            return true;
        }
        return false;
    };

    m_image = QPixmap();
    bool loaded = false;
    for(const QString &root : searchRoots)
    {
        const QString basePath = QDir::cleanPath(root + normalized);
        if(tryLoad(basePath))
        {
            loaded = true;
            break;
        }
        if(!needsExtensionGuess)
            continue;
        for(const QString &ext : kFallbackExtensions)
        {
            if(tryLoad(basePath + ext))
            {
                loaded = true;
                break;
            }
        }
        if(loaded)
            break;
    }

    m_imageSize = QPoint(m_image.width(), m_image.height());
    if(m_imageSize.x() <= 0 || m_imageSize.y() <= 0)
        return;

    if(m_imageCrop.isNull() || m_imageCrop.width() <= 0 || m_imageCrop.height() <= 0)
        m_imageCrop.setRect(0, 0, m_imageSize.x(), m_imageSize.y());
}
