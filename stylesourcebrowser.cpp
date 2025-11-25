#include "stylesourcebrowser.h"

#include <QDir>
#include <QDirIterator>
#include <QHeaderView>
#include <QLabel>
#include <QFileInfo>

namespace {
QString cleanedPath(const QString &path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

constexpr int RolePath = Qt::UserRole;
constexpr int RoleType = Qt::UserRole + 1;
constexpr int RoleStyleName = Qt::UserRole + 2;
}

StyleSourceBrowser::StyleSourceBrowser(QWidget *parent)
    : QFrame(parent), m_tree(new QTreeWidget(this))
{
    setObjectName("styleSourceBrowser");
    setFixedSize(500, 420);
    setLineWidth(0);
    setFrameStyle(0);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *title = new QLabel("Styles Browser", this);
    title->setAlignment(Qt::AlignCenter);
    title->setObjectName("styleSourceTitle");
    layout->addWidget(title);

    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &StyleSourceBrowser::handleItemDoubleClicked);
}

void StyleSourceBrowser::setDataPath(const QString &path)
{
    if(m_dataPath == path)
        return;

    m_dataPath = path;
    refresh();
}

void StyleSourceBrowser::initialize()
{
    refresh();
}

void StyleSourceBrowser::refresh()
{
    rebuildTree();
}

void StyleSourceBrowser::handleItemDoubleClicked(QTreeWidgetItem *item, int)
{
    if(!item)
        return;

    const QString filePath = item->data(0, RolePath).toString();
    if(filePath.isEmpty())
        return;

    const EntryType entryType = static_cast<EntryType>(item->data(0, RoleType).toInt());
    if(entryType == EntryType::Style)
    {
        const QString styleName = item->data(0, RoleStyleName).toString();
        if(!styleName.isEmpty())
            emit styleTemplateActivated(filePath, styleName);
        return;
    }

    emit styleActivated(filePath);
}

void StyleSourceBrowser::rebuildTree()
{
    m_tree->clear();
    m_styleEntries.clear();

    if(m_dataPath.isEmpty())
        return;

    const QDir dataDir(m_dataPath);
    if(dataDir.exists())
    {
        addRootListing("styles", dataDir.filePath("styles"));

        QDir repoDir(dataDir);
        repoDir.cdUp();
        const QString modulesPath = repoDir.filePath("modules");
        addRootListing("modules", modulesPath);
    }
}

void StyleSourceBrowser::addRootListing(const QString &title, const QString &rootPath)
{
    QDir rootDir(rootPath);
    if(!rootDir.exists())
        return;

    const QStringList files = collectOtuiFiles(rootDir.absolutePath());
    if(files.isEmpty())
        return;

    auto *rootItem = new QTreeWidgetItem(QStringList() << QStringLiteral("%1 (%2)").arg(title, cleanedPath(rootDir.absolutePath())));
    rootItem->setDisabled(true);
    m_tree->addTopLevelItem(rootItem);
    for(const QString &file : files)
        addFileEntry(rootItem, rootDir.absolutePath(), file);
    rootItem->setExpanded(true);
}

QStringList StyleSourceBrowser::collectOtuiFiles(const QString &rootPath) const
{
    QStringList files;
    QDirIterator it(rootPath,
                    QStringList() << "*.otui",
                    QDir::Files,
                    QDirIterator::Subdirectories);
    while(it.hasNext())
        files << it.next();
    files.removeDuplicates();
    files.sort(Qt::CaseInsensitive);
    return files;
}

QStringList StyleSourceBrowser::collectStyleEntries(const QString &filePath) const
{
    QString error;
    QStringList styles = m_parser.listStyles(filePath, &error);
    Q_UNUSED(error);
    return styles;
}

void StyleSourceBrowser::addFileEntry(QTreeWidgetItem *parentItem, const QString &rootPath, const QString &filePath)
{
    if(!parentItem)
        return;

    QDir rootDir(rootPath);
    const QString relative = rootDir.relativeFilePath(filePath);
    auto *fileItem = new QTreeWidgetItem(QStringList() << cleanedPath(relative));
    fileItem->setData(0, RolePath, cleanedPath(filePath));
    fileItem->setData(0, RoleType, static_cast<int>(EntryType::File));
    parentItem->addChild(fileItem);

    const QStringList styleEntries = collectStyleEntries(filePath);
    const QString fileLabel = QFileInfo(filePath).fileName();
    for(const QString &styleName : styleEntries)
    {
        const QString display = QStringLiteral("%1 (%2)").arg(styleName, fileLabel);
        auto *styleItem = new QTreeWidgetItem(QStringList() << styleName);
        styleItem->setData(0, RolePath, cleanedPath(filePath));
        styleItem->setData(0, RoleType, static_cast<int>(EntryType::Style));
        styleItem->setData(0, RoleStyleName, styleName);
        fileItem->addChild(styleItem);

        StyleTemplateEntry entry;
        entry.filePath = cleanedPath(filePath);
        entry.styleName = styleName;
        entry.displayName = display;
        m_styleEntries.push_back(entry);
    }

    if(!styleEntries.isEmpty())
        fileItem->setExpanded(false);
}
