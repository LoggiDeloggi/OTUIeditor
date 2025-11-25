#include "parser.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QPoint>
#include <QRect>
#include <QColor>
#include <QRegularExpression>
#include <QStringConverter>
#include <QTextStream>
#include <QSet>
#include <QDir>
#include <QDirIterator>
#include <functional>
#include <QStringList>
#include <memory>
#include <vector>
#include <QFontMetrics>
#include <limits>

#include "mainwindow.h"
#include "button.h"
#include "label.h"
#include "image.h"
#include "item.h"
#include "creature.h"

#include "../thirdparty/otui/otui_parser.h"

namespace OTUI {
namespace {
struct OtuiNodeDeleter
{
    void operator()(OTUINode *node) const
    {
        if(node)
            otui_free(node);
    }
};

struct StyleCacheEntry
{
    QString basePath;
    QHash<QString, const OTUINode*> nodesByName;
    std::vector<std::unique_ptr<OTUINode, OtuiNodeDeleter>> ownedTrees;
};

static QHash<QString, std::shared_ptr<StyleCacheEntry>> g_styleCache;
static thread_local const StyleCacheEntry *g_activeStyleCache = nullptr;
static thread_local QHash<const OTUINode*, const OTUINode*> g_localTemplateBindings;
static thread_local QSet<const OTUINode*> g_templateDefinitionNodes;

QString nodeProperty(const OTUINode *node,
                     const char *key,
                     const QString &fallback = QString());

QString normalizePath(const QString &path)
{
    QString normalized = QDir::fromNativeSeparators(path.trimmed());
    if(normalized.isEmpty())
        return normalized;
    QDir dir(normalized);
    normalized = dir.absolutePath();
    if(normalized.endsWith('/'))
        normalized.chop(1);
    return normalized;
}

void collectStyleNodes(OTUINode *node, QHash<QString, const OTUINode*> &out)
{
    if(!node || !node->name)
        return;

    const QString name = QString::fromUtf8(node->name).trimmed();
    if(!name.isEmpty() && !out.contains(name))
        out.insert(name, node);

    for(size_t i = 0; i < node->nchildren; ++i)
        collectStyleNodes(node->children[i], out);
}

void loadStylesFromDirectory(const QString &directory, StyleCacheEntry &entry)
{
    QDirIterator it(directory,
                    QStringList() << QStringLiteral("*.otui"),
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while(it.hasNext())
    {
        const QString filePath = it.next();
        QByteArray utf8Path = QFile::encodeName(filePath);
        char errBuf[256] = {0};
        OTUINode *root = otui_parse_file(utf8Path.constData(), errBuf, sizeof(errBuf));
        if(!root)
            continue;

        otui_resolve_all_inheritance(root);
        entry.ownedTrees.emplace_back(root);
        collectStyleNodes(root, entry.nodesByName);
    }
}

std::shared_ptr<StyleCacheEntry> ensureStyleCache(const QString &dataPath)
{
    const QString key = normalizePath(dataPath);
    if(key.isEmpty())
        return {};

    auto it = g_styleCache.constFind(key);
    if(it != g_styleCache.constEnd())
        return it.value();

    auto entry = std::make_shared<StyleCacheEntry>();
    entry->basePath = key;
    const QString stylesDir = QDir(key).filePath(QStringLiteral("styles"));
    if(QDir(stylesDir).exists())
        loadStylesFromDirectory(stylesDir, *entry);

    g_styleCache.insert(key, entry);
    return entry;
}

class ScopedStyleContext
{
public:
    explicit ScopedStyleContext(const QString &dataPath)
        : m_previous(g_activeStyleCache)
    {
        if(!dataPath.isEmpty())
            m_cache = ensureStyleCache(dataPath);
        if(m_cache)
            g_activeStyleCache = m_cache.get();
    }

    ~ScopedStyleContext()
    {
        g_activeStyleCache = m_previous;
    }

private:
    const StyleCacheEntry *m_previous = nullptr;
    std::shared_ptr<StyleCacheEntry> m_cache;
};

void buildLocalTemplateBindings(const OTUINode *root,
                                QHash<const OTUINode*, const OTUINode*> &bindings,
                                QSet<const OTUINode*> *templateRoots)
{
    if(!root)
        return;

    QHash<QString, const OTUINode*> templates;
    for(size_t i = 0; i < root->nchildren; ++i)
    {
        const OTUINode *child = root->children[i];
        if(!child || !child->name)
            continue;
        if(!child->base_style)
            continue;
        const QString templateName = QString::fromUtf8(child->name).trimmed();
        if(templateName.isEmpty())
            continue;
        const QString idValue = nodeProperty(child, "id");
        if(!idValue.isEmpty())
            continue;
        if(!templates.contains(templateName))
            templates.insert(templateName, child);
        if(templateRoots)
            templateRoots->insert(child);
    }

    std::function<void(const OTUINode*)> visit = [&](const OTUINode *node) {
        if(!node || !node->name)
            return;
        const QString nodeName = QString::fromUtf8(node->name).trimmed();
        if(!node->base_style && !nodeName.isEmpty())
        {
            const auto it = templates.constFind(nodeName);
            if(it != templates.cend() && *it != node)
                bindings.insert(node, *it);
        }
        for(size_t i = 0; i < node->nchildren; ++i)
            visit(node->children[i]);
    };

    visit(root);
}

class ScopedTemplateBindings
{
public:
    explicit ScopedTemplateBindings(const OTUINode *root)
        : m_previousBindings(g_localTemplateBindings)
        , m_previousTemplates(g_templateDefinitionNodes)
    {
        g_localTemplateBindings.clear();
        g_templateDefinitionNodes.clear();
        buildLocalTemplateBindings(root, g_localTemplateBindings, &g_templateDefinitionNodes);
    }

    ~ScopedTemplateBindings()
    {
        g_localTemplateBindings = m_previousBindings;
        g_templateDefinitionNodes = m_previousTemplates;
    }

private:
    QHash<const OTUINode*, const OTUINode*> m_previousBindings;
    QSet<const OTUINode*> m_previousTemplates;
};

bool isTemplateDefinitionNode(const OTUINode *node)
{
    if(!node || !node->base_style)
        return false;
    const QString idValue = nodeProperty(node, "id");
    return idValue.isEmpty();
}

QString nodeProperty(const OTUINode *node, const char *key, const QString &fallback)
{
    if(!node)
        return fallback;
    const char *value = otui_prop_get(node, key);
    if(!value)
        return fallback;
    return QString::fromUtf8(value).trimmed();
}

QString inheritedNodeProperty(const OTUINode *node,
                              const OTUINode *root,
                              const char *key,
                              const QString &fallback = QString())
{
    const OTUINode *current = node;
    QSet<const OTUINode*> visited;
    QSet<QString> visitedNames;
    while(current)
    {
        const QString value = nodeProperty(current, key);
        if(!value.isEmpty())
            return value;
        if(visited.contains(current))
            break;
        visited.insert(current);
        QString baseName;
        OTUINode *baseNode = nullptr;

        if(current->base_style)
            baseName = QString::fromUtf8(current->base_style).trimmed();

        if(!baseName.isEmpty())
        {
            if(visitedNames.contains(baseName))
                break;
            visitedNames.insert(baseName);
            baseNode = root ? otui_find_node(const_cast<OTUINode*>(root), current->base_style) : nullptr;
            if(!baseNode && g_activeStyleCache)
            {
                const auto it = g_activeStyleCache->nodesByName.constFind(baseName);
                if(it != g_activeStyleCache->nodesByName.constEnd())
                    baseNode = const_cast<OTUINode*>(*it);
            }
        }

        if(!baseNode)
        {
            const auto localIt = g_localTemplateBindings.constFind(current);
            if(localIt != g_localTemplateBindings.cend())
            {
                baseNode = const_cast<OTUINode*>(localIt.value());
                baseName = QString::fromUtf8(baseNode->name).trimmed();
                if(!baseName.isEmpty())
                {
                    if(visitedNames.contains(baseName))
                        break;
                    visitedNames.insert(baseName);
                }
            }
        }

        if(!baseNode && g_activeStyleCache && current && current->name)
        {
            const QString currentName = QString::fromUtf8(current->name).trimmed();
            if(!currentName.isEmpty() && !visitedNames.contains(currentName))
            {
                const auto styleIt = g_activeStyleCache->nodesByName.constFind(currentName);
                if(styleIt != g_activeStyleCache->nodesByName.cend() && *styleIt != current)
                {
                    baseNode = const_cast<OTUINode*>(*styleIt);
                    visitedNames.insert(currentName);
                }
            }
        }

        if(!baseNode || visited.contains(baseNode))
            break;
        current = baseNode;
    }
    return fallback;
}

bool nodeBool(const OTUINode *node, const char *key, bool fallback)
{
    const QString value = nodeProperty(node, key);
    if(value.isEmpty())
        return fallback;
    if(value.compare("true", Qt::CaseInsensitive) == 0 || value == QStringLiteral("1"))
        return true;
    if(value.compare("false", Qt::CaseInsensitive) == 0 || value == QStringLiteral("0"))
        return false;
    return fallback;
}

bool inheritedNodeBool(const OTUINode *node,
                       const OTUINode *root,
                       const char *key,
                       bool fallback)
{
    const QString value = inheritedNodeProperty(node, root, key, QString());
    if(value.isEmpty())
        return fallback;
    if(value.compare("true", Qt::CaseInsensitive) == 0 || value == QStringLiteral("1"))
        return true;
    if(value.compare("false", Qt::CaseInsensitive) == 0 || value == QStringLiteral("0"))
        return false;
    return fallback;
}

double nodeDouble(const OTUINode *node, const char *key, double fallback)
{
    const QString value = nodeProperty(node, key);
    if(value.isEmpty())
        return fallback;
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : fallback;
}

double inheritedNodeDouble(const OTUINode *node,
                           const OTUINode *root,
                           const char *key,
                           double fallback)
{
    const QString value = inheritedNodeProperty(node, root, key, QString());
    if(value.isEmpty())
        return fallback;
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    return ok ? parsed : fallback;
}

QPoint parsePoint(const QString &value, const QPoint &fallback = QPoint())
{
    const QStringList parts = value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if(parts.size() != 2)
        return fallback;
    bool okX = false;
    bool okY = false;
    const int x = parts.at(0).toInt(&okX);
    const int y = parts.at(1).toInt(&okY);
    if(!okX || !okY)
        return fallback;
    return QPoint(x, y);
}

QRect parseRectFour(const QString &value, const QRect &fallback = QRect())
{
    const QStringList parts = value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if(parts.size() != 4)
        return fallback;
    bool ok[4] = { false, false, false, false };
    int values[4];
    for(int i = 0; i < 4; ++i)
        values[i] = parts.at(i).toInt(&ok[i]);
    if(!ok[0] || !ok[1] || !ok[2] || !ok[3])
        return fallback;
    return QRect(values[0], values[1], values[2], values[3]);
}

Qt::Alignment parseAlignment(const QString &value, Qt::Alignment fallback)
{
    if(value.isEmpty())
        return fallback;

    Qt::Alignment alignment = Qt::Alignment();
    bool hasHorizontal = false;
    bool hasVertical = false;
    const QStringList tokens = value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for(const QString &tokenRaw : tokens)
    {
        const QString token = tokenRaw.trimmed().toLower();
        if(token == QStringLiteral("left"))
        {
            alignment |= Qt::AlignLeft;
            hasHorizontal = true;
        }
        else if(token == QStringLiteral("right"))
        {
            alignment |= Qt::AlignRight;
            hasHorizontal = true;
        }
        else if(token == QStringLiteral("center"))
        {
            alignment |= Qt::AlignHCenter;
            alignment |= Qt::AlignVCenter;
            hasHorizontal = true;
            hasVertical = true;
        }
        else if(token == QStringLiteral("hcenter") || token == QStringLiteral("horizontalcenter"))
        {
            alignment |= Qt::AlignHCenter;
            hasHorizontal = true;
        }
        else if(token == QStringLiteral("vcenter") || token == QStringLiteral("verticalcenter"))
        {
            alignment |= Qt::AlignVCenter;
            hasVertical = true;
        }
        else if(token == QStringLiteral("top"))
        {
            alignment |= Qt::AlignTop;
            hasVertical = true;
        }
        else if(token == QStringLiteral("bottom"))
        {
            alignment |= Qt::AlignBottom;
            hasVertical = true;
        }
    }

    if(!hasHorizontal)
        alignment |= (fallback & (Qt::AlignLeft | Qt::AlignRight | Qt::AlignHCenter | Qt::AlignJustify));
    if(!hasVertical)
        alignment |= (fallback & (Qt::AlignTop | Qt::AlignBottom | Qt::AlignVCenter));

    if(alignment == Qt::Alignment())
        return fallback;
    return alignment;
}

QFont parseFontDescriptor(const QString &value, const QFont &fallback)
{
    if(value.isEmpty())
        return fallback;

    QFont font = fallback;
    QStringList segments = value.split('-', Qt::SkipEmptyParts);
    if(segments.isEmpty())
        return font;

    const QString family = segments.takeFirst().trimmed().replace('_', ' ');
    if(!family.isEmpty())
        font.setFamily(family);

    for(const QString &segmentRaw : segments)
    {
        const QString segment = segmentRaw.trimmed();
        const QString lower = segment.toLower();
        bool ok = false;
        if(lower.endsWith(QStringLiteral("px")))
        {
            const int size = lower.left(lower.size() - 2).toInt(&ok);
            if(ok)
                font.setPixelSize(size);
        }
        else if(lower == QStringLiteral("bold"))
        {
            font.setBold(true);
        }
        else if(lower == QStringLiteral("italic"))
        {
            font.setItalic(true);
        }
        else if(lower == QStringLiteral("underline"))
        {
            font.setUnderline(true);
        }
        else if(lower == QStringLiteral("monospace") || lower == QStringLiteral("monospaced"))
        {
            font.setStyleHint(QFont::TypeWriter);
        }
        else if(lower == QStringLiteral("monochrome"))
        {
            font.setStyleStrategy(QFont::NoAntialias);
        }
        else if(lower == QStringLiteral("antialised") || lower == QStringLiteral("antialiased"))
        {
            font.setStyleStrategy(QFont::PreferAntialias);
        }
    }

    return font;
}

void applyTextAutoResize(OTUI::Widget *widget)
{
    if(!widget || !widget->supportsTextProperty())
        return;
    if(!widget->textAutoResize())
        return;

    const QString text = widget->textProperty();
    if(text.isEmpty())
        return;

    QFontMetrics metrics(widget->getFont());
    QRect bounds;
    if(widget->textWrap() && widget->getSize().x() > 0)
        bounds = metrics.boundingRect(QRect(0, 0, widget->getSize().x(), std::numeric_limits<int>::max()), Qt::TextWordWrap, text);
    else
        bounds = metrics.boundingRect(text);

    QPoint newSize = widget->getSize();
    if(bounds.width() > 0)
        newSize.setX(bounds.width() + widget->textOffset().x());
    if(bounds.height() > 0)
        newSize.setY(bounds.height() + widget->textOffset().y());
    widget->setSizeProperty(newSize);
}

int parseInt(const QString &value, int fallback = 0)
{
    bool ok = false;
    const int result = value.toInt(&ok);
    return ok ? result : fallback;
}

bool tryParseInt(const QString &value, int &out)
{
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if(!ok)
        return false;
    out = parsed;
    return true;
}

enum class EdgeGroupType
{
    Margin,
    Padding
};

struct EdgeValues
{
    int top = 0;
    int right = 0;
    int bottom = 0;
    int left = 0;
};

bool parseEdgeValues(const QString &value, EdgeValues &out)
{
    const QStringList parts = value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const int count = parts.size();
    if(count < 1 || count > 4)
        return false;

    int parsed[4] = { 0, 0, 0, 0 };
    for(int i = 0; i < count; ++i)
    {
        bool ok = false;
        parsed[i] = parts.at(i).toInt(&ok);
        if(!ok)
            return false;
    }

    switch(count)
    {
    case 1:
        out.top = out.right = out.bottom = out.left = parsed[0];
        return true;
    case 2:
        out.top = out.bottom = parsed[0];
        out.right = out.left = parsed[1];
        return true;
    case 3:
        out.top = parsed[0];
        out.right = out.left = parsed[1];
        out.bottom = parsed[2];
        return true;
    case 4:
        out.top = parsed[0];
        out.right = parsed[1];
        out.bottom = parsed[2];
        out.left = parsed[3];
        return true;
    default:
        return false;
    }
}

QRect parseImageBorderRect(const QString &value, const QRect &fallback = QRect())
{
    EdgeValues values;
    if(!parseEdgeValues(value, values))
        return fallback;
    return QRect(values.left, values.top, values.right, values.bottom);
}

void assignEdgeValue(OTUI::Widget *widget, EdgeGroupType type, OTUI::AnchorEdge edge, int value)
{
    if(!widget)
        return;

    switch(type)
    {
    case EdgeGroupType::Margin:
        switch(edge)
        {
        case OTUI::AnchorEdge::Left:
            widget->setMarginLeft(value);
            break;
        case OTUI::AnchorEdge::Right:
            widget->setMarginRight(value);
            break;
        case OTUI::AnchorEdge::Top:
            widget->setMarginTop(value);
            break;
        case OTUI::AnchorEdge::Bottom:
            widget->setMarginBottom(value);
            break;
        default:
            break;
        }
        break;
    case EdgeGroupType::Padding:
        switch(edge)
        {
        case OTUI::AnchorEdge::Left:
            widget->setPaddingLeft(value);
            break;
        case OTUI::AnchorEdge::Right:
            widget->setPaddingRight(value);
            break;
        case OTUI::AnchorEdge::Top:
            widget->setPaddingTop(value);
            break;
        case OTUI::AnchorEdge::Bottom:
            widget->setPaddingBottom(value);
            break;
        default:
            break;
        }
        break;
    }
}

void applyEdgeGroupProperty(OTUI::Widget *widget, EdgeGroupType type, const QString &value)
{
    EdgeValues values;
    if(!parseEdgeValues(value, values))
        return;

    assignEdgeValue(widget, type, OTUI::AnchorEdge::Top, values.top);
    assignEdgeValue(widget, type, OTUI::AnchorEdge::Right, values.right);
    assignEdgeValue(widget, type, OTUI::AnchorEdge::Bottom, values.bottom);
    assignEdgeValue(widget, type, OTUI::AnchorEdge::Left, values.left);
}

void applyEdgeComponentProperty(OTUI::Widget *widget,
                                EdgeGroupType type,
                                OTUI::AnchorEdge edge,
                                const QString &value)
{
    int parsed = 0;
    if(!tryParseInt(value, parsed))
        return;
    assignEdgeValue(widget, type, edge, parsed);
}

OTUI::AnchorEdge parseAnchorEdge(const QString &name)
{
    const QString edgeName = name.trimmed().toLower();
    if(edgeName == QStringLiteral("left"))
        return OTUI::AnchorEdge::Left;
    if(edgeName == QStringLiteral("right"))
        return OTUI::AnchorEdge::Right;
    if(edgeName == QStringLiteral("top"))
        return OTUI::AnchorEdge::Top;
    if(edgeName == QStringLiteral("bottom"))
        return OTUI::AnchorEdge::Bottom;
    if(edgeName == QStringLiteral("horizontalcenter"))
        return OTUI::AnchorEdge::HorizontalCenter;
    if(edgeName == QStringLiteral("verticalcenter"))
        return OTUI::AnchorEdge::VerticalCenter;
    return OTUI::AnchorEdge::None;
}

struct AnchorDescriptor
{
    QString targetId;
    OTUI::AnchorEdge edge = OTUI::AnchorEdge::None;

    bool isValid() const { return !targetId.isEmpty() && edge != OTUI::AnchorEdge::None; }
};

AnchorDescriptor parseAnchorDescriptor(const QString &value)
{
    AnchorDescriptor descriptor;
    const QString trimmed = value.trimmed();
    const QStringList parts = trimmed.split('.', Qt::SkipEmptyParts);
    if(parts.size() != 2)
        return descriptor;
    descriptor.targetId = parts.at(0).trimmed();
    descriptor.edge = parseAnchorEdge(parts.at(1));
    return descriptor;
}

void bindAnchorEdge(OTUI::Widget *widget,
                    OTUI::AnchorEdge sourceEdge,
                    const QString &value)
{
    if(!widget)
        return;

    if(value.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0)
    {
        widget->setAnchorBinding(sourceEdge, QString(), OTUI::AnchorEdge::None);
        return;
    }

    const AnchorDescriptor descriptor = parseAnchorDescriptor(value);
    if(!descriptor.isValid())
        return;

    widget->setAnchorBinding(sourceEdge, descriptor.targetId, descriptor.edge);
}

void applyAnchorProperty(OTUI::Widget *widget,
                         const QString &propertyName,
                         const QString &value)
{
    if(!widget)
        return;

    const QString trimmedValue = value.trimmed();
    if(trimmedValue.isEmpty())
        return;

    const QString lowerProp = propertyName.trimmed().toLower();
    if(lowerProp == QStringLiteral("anchors.fill"))
    {
        if(trimmedValue.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0)
        {
            widget->clearAnchors();
            return;
        }
        widget->setAnchorBinding(OTUI::AnchorEdge::Left, trimmedValue, OTUI::AnchorEdge::Left);
        widget->setAnchorBinding(OTUI::AnchorEdge::Right, trimmedValue, OTUI::AnchorEdge::Right);
        widget->setAnchorBinding(OTUI::AnchorEdge::Top, trimmedValue, OTUI::AnchorEdge::Top);
        widget->setAnchorBinding(OTUI::AnchorEdge::Bottom, trimmedValue, OTUI::AnchorEdge::Bottom);
        return;
    }

    if(lowerProp == QStringLiteral("anchors.centerin"))
    {
        if(trimmedValue.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0)
        {
            widget->setAnchorBinding(OTUI::AnchorEdge::HorizontalCenter, QString(), OTUI::AnchorEdge::None);
            widget->setAnchorBinding(OTUI::AnchorEdge::VerticalCenter, QString(), OTUI::AnchorEdge::None);
            return;
        }
        widget->setAnchorBinding(OTUI::AnchorEdge::HorizontalCenter,
                                 trimmedValue,
                                 OTUI::AnchorEdge::HorizontalCenter);
        widget->setAnchorBinding(OTUI::AnchorEdge::VerticalCenter,
                                 trimmedValue,
                                 OTUI::AnchorEdge::VerticalCenter);
        return;
    }

    auto bind = [&](OTUI::AnchorEdge edge) {
        bindAnchorEdge(widget, edge, trimmedValue);
    };

    if(lowerProp == QStringLiteral("anchors.left"))
        bind(OTUI::AnchorEdge::Left);
    else if(lowerProp == QStringLiteral("anchors.right"))
        bind(OTUI::AnchorEdge::Right);
    else if(lowerProp == QStringLiteral("anchors.top"))
        bind(OTUI::AnchorEdge::Top);
    else if(lowerProp == QStringLiteral("anchors.bottom"))
        bind(OTUI::AnchorEdge::Bottom);
    else if(lowerProp == QStringLiteral("anchors.horizontalcenter"))
        bind(OTUI::AnchorEdge::HorizontalCenter);
    else if(lowerProp == QStringLiteral("anchors.verticalcenter"))
        bind(OTUI::AnchorEdge::VerticalCenter);
}

void resolveAnchors(OTUI::Parser::WidgetList &widgets)
{
    QHash<QString, OTUI::Widget*> lookup;
    lookup.reserve(static_cast<int>(widgets.size()));
    for(const auto &widget : widgets)
    {
        if(widget)
            lookup.insert(widget->getId(), widget.get());
    }

    auto findPreviousSibling = [&](const OTUI::Widget *target) -> OTUI::Widget* {
        if(!target)
            return nullptr;
        OTUI::Widget *parent = target->getParent();
        OTUI::Widget *previous = nullptr;
        for(const auto &entry : widgets)
        {
            OTUI::Widget *candidate = entry.get();
            if(candidate == target)
                break;
            if(candidate && candidate->getParent() == parent)
                previous = candidate;
        }
        return previous;
    };

    for(const auto &widget : widgets)
    {
        if(!widget)
            continue;
        widget->applyAnchors([&](const QString &id) -> OTUI::Widget* {
            if(id.compare(QStringLiteral("prev"), Qt::CaseInsensitive) == 0 ||
               id.compare(QStringLiteral("previous"), Qt::CaseInsensitive) == 0)
            {
                return findPreviousSibling(widget.get());
            }
            return lookup.value(id, nullptr);
        });
    }
}

template<typename WidgetType>
std::unique_ptr<WidgetType> createWidget(const QString &id,
                                         const QString &dataPath,
                                         const QString &imageSource)
{
    auto widget = std::make_unique<WidgetType>(id, dataPath, imageSource);
    widget->setId(id);
    return widget;
}

std::unique_ptr<OTUI::Widget> createBaseWidget(const QString &id,
                                               const QString &dataPath,
                                               const QString &imageSource)
{
    auto widget = std::make_unique<OTUI::Widget>(id, dataPath, imageSource);
    widget->setId(id);
    return widget;
}

std::unique_ptr<OTUI::Widget> createWidgetForNode(const QString &nodeName,
                                                  const QString &widgetId,
                                                  const QString &dataPath,
                                                  const QString &imageSource)
{
    if(nodeName.compare("MainWindow", Qt::CaseInsensitive) == 0 ||
       nodeName.compare("UIWindow", Qt::CaseInsensitive) == 0)
        return createWidget<OTUI::MainWindow>(widgetId, dataPath, imageSource);
    if(nodeName.compare("Button", Qt::CaseInsensitive) == 0 ||
       nodeName.compare("UIButton", Qt::CaseInsensitive) == 0)
        return createWidget<OTUI::Button>(widgetId, dataPath, imageSource);
    if(nodeName.compare("Label", Qt::CaseInsensitive) == 0 ||
       nodeName.compare("UILabel", Qt::CaseInsensitive) == 0)
        return createWidget<OTUI::Label>(widgetId, dataPath, imageSource);
    if(nodeName.compare("Image", Qt::CaseInsensitive) == 0 ||
       nodeName.compare("UIImage", Qt::CaseInsensitive) == 0)
        return createWidget<OTUI::Image>(widgetId, dataPath, imageSource);
    if(nodeName.compare("Item", Qt::CaseInsensitive) == 0 ||
       nodeName.compare("UIItem", Qt::CaseInsensitive) == 0)
        return createWidget<OTUI::Item>(widgetId, dataPath, imageSource);
    if(nodeName.compare("Creature", Qt::CaseInsensitive) == 0 ||
       nodeName.compare("UICreature", Qt::CaseInsensitive) == 0)
        return createWidget<OTUI::Creature>(widgetId, dataPath, imageSource);

    return createBaseWidget(widgetId, dataPath, imageSource);
}

void applyCommonWidgetProps(OTUI::Widget *widget,
                            const OTUINode *node,
                            const OTUINode *root,
                            const QString &dataPath)
{
    if(!widget || !node)
        return;

    widget->setIdProperty(nodeProperty(node, "id", widget->getId()));

    const QString fontValue = inheritedNodeProperty(node, root, "font");
    if(!fontValue.isEmpty())
        widget->setFont(parseFontDescriptor(fontValue, widget->getFont()));

    const QString posValue = inheritedNodeProperty(node, root, "position");
    if(!posValue.isEmpty())
        widget->setPos(parsePoint(posValue, widget->getPos()));

    const QString sizeValue = inheritedNodeProperty(node, root, "size");
    if(!sizeValue.isEmpty())
        widget->setSizeProperty(parsePoint(sizeValue, widget->getSizeProperty()));

    widget->setOpacity(static_cast<float>(inheritedNodeDouble(node, root, "opacity", widget->opacity())));
    widget->setVisibleProperty(inheritedNodeBool(node, root, "visible", widget->isVisible()));

    const QString textValue = inheritedNodeProperty(node, root, "text");
    if(!textValue.isEmpty() && widget->supportsTextProperty())
        widget->setTextProperty(textValue);

    const QString textAlignValue = inheritedNodeProperty(node, root, "text-align");
    if(!textAlignValue.isEmpty())
        widget->setTextAlignment(parseAlignment(textAlignValue, widget->textAlignment()));

    const QString textOffsetValue = inheritedNodeProperty(node, root, "text-offset");
    if(!textOffsetValue.isEmpty())
        widget->setTextOffset(parsePoint(textOffsetValue, widget->textOffset()));

    widget->setTextWrap(inheritedNodeBool(node, root, "text-wrap", widget->textWrap()));
    widget->setTextAutoResize(inheritedNodeBool(node, root, "text-auto-resize", widget->textAutoResize()));

    if(widget->supportsTextProperty())
        applyTextAutoResize(widget);

    const QString imageSource = inheritedNodeProperty(node, root, "image-source");
    if(!imageSource.isEmpty())
        widget->setImageSource(imageSource, dataPath);

    const QString cropValue = inheritedNodeProperty(node, root, "image-clip");
    if(!cropValue.isEmpty())
        widget->setImageCrop(parseRectFour(cropValue, widget->getImageCrop()));

    const QString borderValue = inheritedNodeProperty(node, root, "image-border");
    if(!borderValue.isEmpty())
        widget->setImageBorder(parseImageBorderRect(borderValue, widget->getImageBorder()));

    auto applyBorderComponent = [&](const QString &value, auto setter) {
        if(value.isEmpty())
            return;
        int parsed = 0;
        if(!tryParseInt(value, parsed))
            return;
        QRect border = widget->getImageBorder();
        setter(border, parsed);
        widget->setImageBorder(border);
    };

    applyBorderComponent(inheritedNodeProperty(node, root, "image-border-top"), [](QRect &border, int value) {
        border.setY(value);
    });
    applyBorderComponent(inheritedNodeProperty(node, root, "image-border-right"), [](QRect &border, int value) {
        border.setWidth(value);
    });
    applyBorderComponent(inheritedNodeProperty(node, root, "image-border-bottom"), [](QRect &border, int value) {
        border.setHeight(value);
    });
    applyBorderComponent(inheritedNodeProperty(node, root, "image-border-left"), [](QRect &border, int value) {
        border.setX(value);
    });

    const QString positionX = inheritedNodeProperty(node, root, "x");
    if(!positionX.isEmpty())
    {
        QPoint pos = widget->getPos();
        pos.setX(parseInt(positionX, pos.x()));
        widget->setPos(pos);
    }

    const QString positionY = inheritedNodeProperty(node, root, "y");
    if(!positionY.isEmpty())
    {
        QPoint pos = widget->getPos();
        pos.setY(parseInt(positionY, pos.y()));
        widget->setPos(pos);
    }

    const QString marginValue = inheritedNodeProperty(node, root, "margin");
    if(!marginValue.isEmpty())
        applyEdgeGroupProperty(widget, EdgeGroupType::Margin, marginValue);

    struct EdgePropertyDef
    {
        const char *name;
        OTUI::AnchorEdge edge;
    };

    const EdgePropertyDef marginProps[] = {
        { "margin-top", OTUI::AnchorEdge::Top },
        { "margin-right", OTUI::AnchorEdge::Right },
        { "margin-bottom", OTUI::AnchorEdge::Bottom },
        { "margin-left", OTUI::AnchorEdge::Left }
    };

    for(const EdgePropertyDef &prop : marginProps)
    {
        const QString value = inheritedNodeProperty(node, root, prop.name);
        if(!value.isEmpty())
            applyEdgeComponentProperty(widget, EdgeGroupType::Margin, prop.edge, value);
    }

    const QString paddingValue = inheritedNodeProperty(node, root, "padding");
    if(!paddingValue.isEmpty())
        applyEdgeGroupProperty(widget, EdgeGroupType::Padding, paddingValue);

    const EdgePropertyDef paddingProps[] = {
        { "padding-top", OTUI::AnchorEdge::Top },
        { "padding-right", OTUI::AnchorEdge::Right },
        { "padding-bottom", OTUI::AnchorEdge::Bottom },
        { "padding-left", OTUI::AnchorEdge::Left }
    };

    for(const EdgePropertyDef &prop : paddingProps)
    {
        const QString value = inheritedNodeProperty(node, root, prop.name);
        if(!value.isEmpty())
            applyEdgeComponentProperty(widget, EdgeGroupType::Padding, prop.edge, value);
    }

    const char *anchorProps[] = {
        "anchors.left",
        "anchors.right",
        "anchors.top",
        "anchors.bottom",
        "anchors.horizontalCenter",
        "anchors.verticalCenter",
        "anchors.centerIn",
        "anchors.fill"
    };

    for(const char *anchorName : anchorProps)
    {
        const QString value = inheritedNodeProperty(node, root, anchorName);
        if(!value.isEmpty())
            applyAnchorProperty(widget, QString::fromLatin1(anchorName), value);
    }

    widget->setPhantom(inheritedNodeBool(node, root, "phantom", widget->isPhantom()));

    const QString colorValue = inheritedNodeProperty(node, root, "color");
    if(!colorValue.isEmpty())
    {
        QColor parsed(colorValue.trimmed());
        if(parsed.isValid())
            widget->setColor(parsed);
    }
}

void buildWidgetsFromNode(const OTUINode *node,
                          const OTUINode *root,
                          OTUI::Widget *parent,
                          const QString &dataPath,
                          OTUI::Parser::WidgetList &outWidgets,
                          bool skipTopLevelTemplates = true)
{
    if(!node)
        return;

    const QString nodeName = QString::fromUtf8(node->name);
    if(!parent && skipTopLevelTemplates)
    {
        if(g_templateDefinitionNodes.contains(node) || isTemplateDefinitionNode(node))
            return;
    }
    QString widgetId = nodeProperty(node, "id", nodeName);
    if(widgetId.isEmpty())
        widgetId = nodeName;
    const QString imageSource = nodeProperty(node, "image-source");

    std::unique_ptr<OTUI::Widget> widget = createWidgetForNode(nodeName, widgetId, dataPath, imageSource);
    applyCommonWidgetProps(widget.get(), node, root, dataPath);
    if(parent)
        widget->setParent(parent);

    OTUI::Widget *rawPtr = widget.get();
    outWidgets.emplace_back(std::move(widget));

    for(size_t i = 0; i < node->nchildren; ++i)
        buildWidgetsFromNode(node->children[i], root, rawPtr, dataPath, outWidgets, skipTopLevelTemplates);
}

void releaseTree(OTUINode *root)
{
    otui_free(root);
}
}

bool Parser::loadFromFile(const QString& path,
                          WidgetList& outWidgets,
                          QString* error,
                          const QString& dataPath) const
{
    QByteArray utf8Path = QFile::encodeName(path);
    char errBuf[256] = {0};
    OTUINode *root = otui_parse_file(utf8Path.constData(), errBuf, sizeof(errBuf));
    if(!root)
    {
        if(error)
            *error = QString::fromUtf8(errBuf).trimmed();
        return false;
    }

    otui_resolve_all_inheritance(root);

    std::unique_ptr<OTUINode, decltype(&releaseTree)> guard(root, releaseTree);
    outWidgets.clear();

    QHash<const OTUINode*, OTUI::Widget*> createdWidgets;

    std::function<void(const OTUINode*, OTUI::Widget*)> visitNode;
    ScopedStyleContext styleContext(dataPath);
    ScopedTemplateBindings templateBindings(root);
    visitNode = [&](const OTUINode *node, OTUI::Widget *parent) {
        if(!node)
            return;
        if(node == root)
        {
            for(size_t i = 0; i < node->nchildren; ++i)
                visitNode(node->children[i], nullptr);
            return;
        }

        const QString nodeName = QString::fromUtf8(node->name);
        const QString explicitId = nodeProperty(node, "id");
        if(!parent)
        {
            if(g_templateDefinitionNodes.contains(node))
                return;
            if(explicitId.isEmpty() && node->base_style)
                return;
        }
        const QString widgetId = explicitId.isEmpty() ? nodeName : explicitId;
        const QString imageSource = nodeProperty(node, "image-source");

        std::unique_ptr<OTUI::Widget> widget = createWidgetForNode(nodeName, widgetId, dataPath, imageSource);

        applyCommonWidgetProps(widget.get(), node, root, dataPath);

        if(parent)
            widget->setParent(parent);

        OTUI::Widget *rawPtr = widget.get();
        createdWidgets.insert(node, rawPtr);
        outWidgets.emplace_back(std::move(widget));

        for(size_t i = 0; i < node->nchildren; ++i)
            visitNode(node->children[i], rawPtr);
    };

    visitNode(root, nullptr);
    resolveAnchors(outWidgets);
    return true;
}

bool Parser::instantiateStyle(const QString &path,
                              const QString &styleName,
                              WidgetList &outWidgets,
                              QString *error,
                              const QString &dataPath) const
{
    QByteArray utf8Path = QFile::encodeName(path);
    char errBuf[256] = {0};
    OTUINode *root = otui_parse_file(utf8Path.constData(), errBuf, sizeof(errBuf));
    if(!root)
    {
        if(error)
            *error = QString::fromUtf8(errBuf).trimmed();
        return false;
    }
    otui_resolve_all_inheritance(root);

    std::unique_ptr<OTUINode, decltype(&releaseTree)> guard(root, releaseTree);
    const QString targetName = styleName.trimmed();
    if(targetName.isEmpty())
    {
        if(error)
            *error = QObject::tr("Invalid style name.");
        return false;
    }

    const OTUINode *targetNode = nullptr;
    for(size_t i = 0; i < root->nchildren; ++i)
    {
        const QString nodeName = QString::fromUtf8(root->children[i]->name);
        if(nodeName.compare(targetName, Qt::CaseInsensitive) == 0)
        {
            targetNode = root->children[i];
            break;
        }
    }

    if(!targetNode)
    {
        if(error)
            *error = QObject::tr("Style '%1' not found in %2.").arg(targetName, path);
        return false;
    }

    outWidgets.clear();
    ScopedStyleContext styleContext(dataPath);
    ScopedTemplateBindings templateBindings(root);
    buildWidgetsFromNode(targetNode, root, nullptr, dataPath, outWidgets, false);
    if(outWidgets.empty())
    {
        if(error)
            *error = QObject::tr("Failed to instantiate style '%1'.").arg(targetName);
        return false;
    }

    resolveAnchors(outWidgets);
    return true;
}

QStringList Parser::listStyles(const QString &path, QString *error) const
{
    QStringList styles;
    QByteArray utf8Path = QFile::encodeName(path);
    char errBuf[256] = {0};
    OTUINode *root = otui_parse_file(utf8Path.constData(), errBuf, sizeof(errBuf));
    if(!root)
    {
        if(error)
            *error = QString::fromUtf8(errBuf).trimmed();
        return styles;
    }

    std::unique_ptr<OTUINode, decltype(&releaseTree)> guard(root, releaseTree);
    for(size_t i = 0; i < root->nchildren; ++i)
    {
        const QString nodeName = QString::fromUtf8(root->children[i]->name).trimmed();
        if(!nodeName.isEmpty())
            styles << nodeName;
    }

    styles.removeDuplicates();
    styles.sort(Qt::CaseInsensitive);
    return styles;
}

Parser::WidgetPtr Parser::createPlaceholderWidget(const QString &fileStem) const
{
    return createBaseWidget(fileStem, QString(), QString());
}

static void serializeNode(QTextStream &stream, const OTUI::Widget *widget)
{
    stream << widget->getId() << Qt::endl;
    stream << "  id: " << widget->getId() << Qt::endl;
    stream << "  position: " << widget->x() << " " << widget->y() << Qt::endl;
    stream << "  size: " << widget->width() << " " << widget->height() << Qt::endl;
    stream << "  opacity: " << widget->opacity() << Qt::endl;
    stream << "  visible: " << (widget->isVisible() ? "true" : "false") << Qt::endl;
    if(widget->supportsTextProperty())
    {
        const QString text = widget->textProperty();
        if(!text.isEmpty())
            stream << "  text: " << text << Qt::endl;
    }
    if(!widget->imageSource().isEmpty())
        stream << "  image-source: " << widget->imageSource() << Qt::endl;
    if(!widget->getImageCrop().isNull())
    {
        const QRect crop = widget->getImageCrop();
        stream << "  image-clip: "
               << crop.x() << " " << crop.y() << " "
               << crop.width() << " " << crop.height() << Qt::endl;
    }
    if(!widget->getImageBorder().isNull())
    {
        const QRect border = widget->getImageBorder();
        stream << "  image-border: "
               << border.x() << " " << border.y() << " "
               << border.width() << " " << border.height() << Qt::endl;
    }

    if(widget->isPhantom())
        stream << "  phantom: true" << Qt::endl;

    const QString colorValue = widget->colorString();
    if(!colorValue.isEmpty())
        stream << "  color: " << colorValue << Qt::endl;

    auto writeEdgeGroup = [&](const char *prefix, const OTUI::EdgeGroup<int> &group) {
        if(group.top == 0 && group.right == 0 && group.bottom == 0 && group.left == 0)
            return;
        stream << "  " << prefix << "-top: " << group.top << Qt::endl;
        stream << "  " << prefix << "-right: " << group.right << Qt::endl;
        stream << "  " << prefix << "-bottom: " << group.bottom << Qt::endl;
        stream << "  " << prefix << "-left: " << group.left << Qt::endl;
    };

    writeEdgeGroup("margin", widget->margin());
    writeEdgeGroup("padding", widget->padding());

    const QString fillTarget = widget->fillTarget();
    if(!fillTarget.isEmpty())
        stream << "  anchors.fill: " << fillTarget << Qt::endl;

    const QString centerTarget = widget->centerInTarget();
    if(!centerTarget.isEmpty())
        stream << "  anchors.centerIn: " << centerTarget << Qt::endl;

    auto writeAnchor = [&](OTUI::AnchorEdge edge, const char *name) {
        const QString descriptor = widget->anchorDescriptor(edge);
        if(descriptor.isEmpty())
            return;
        stream << "  anchors." << name << ": " << descriptor << Qt::endl;
    };

    if(fillTarget.isEmpty())
    {
        writeAnchor(OTUI::AnchorEdge::Left, "left");
        writeAnchor(OTUI::AnchorEdge::Right, "right");
        writeAnchor(OTUI::AnchorEdge::Top, "top");
        writeAnchor(OTUI::AnchorEdge::Bottom, "bottom");
    }

    if(centerTarget.isEmpty())
    {
        writeAnchor(OTUI::AnchorEdge::HorizontalCenter, "horizontalCenter");
        writeAnchor(OTUI::AnchorEdge::VerticalCenter, "verticalCenter");
    }

    stream << Qt::endl;
}

bool Parser::saveToFile(const QString& path, const WidgetList& widgets, QString* error) const
{
    if(path.isEmpty()) {
        if(error) {
            *error = QObject::tr("Invalid destination path.");
        }
        return false;
    }

    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if(error) {
            *error = QObject::tr("Unable to save file: %1").arg(file.errorString());
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << "# OTUIEditor export\n";

    for(const auto& widget : widgets) {
        if(!widget) {
            continue;
        }
        serializeNode(stream, widget.get());
    }

    return true;
}

} // namespace OTUI
