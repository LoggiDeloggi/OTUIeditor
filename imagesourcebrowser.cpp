#include "imagesourcebrowser.h"

#include <QPixmapCache>
#include <QHeaderView>
#include <QDir>
#include <QTableWidgetItem>

ImageSourceBrowser::ImageSourceBrowser(QWidget *parent) : QFrame(parent)
{
}

ImageSourceBrowser::~ImageSourceBrowser()
{
}

void ImageSourceBrowser::initialize()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    this->setObjectName("imageSourceBrowser");
    this->setFixedSize(800, 500);
    this->setLineWidth(0);
    this->setFrameStyle(0);
    this->setFrameShadow(QFrame::Plain);

    topBar = new QFrame(this);
    topBar->setObjectName("topBar");
    topBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    topBar->setMinimumHeight(20);
    topBar->setMaximumHeight(20);
    topBar->setLineWidth(0);

    QHBoxLayout *topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setSpacing(0);
    topBarLayout->setContentsMargins(3, 0, 3, 0);

    titleLabel = new QLabel(topBar);
    titleLabel->setText("Image Source Browser");

    closeButton = new QPushButton("X", topBar);
    closeButton->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    closeButton->setMaximumSize(16, 16);
    closeButton->setFlat(true);
    closeButton->setCursor(Qt::PointingHandCursor);

    connect(closeButton, &QPushButton::released, this, &ImageSourceBrowser::handleCloseButton);

    topBarLayout->addWidget(titleLabel);
    topBarLayout->addWidget(closeButton);
    topBarLayout->setAlignment(closeButton, Qt::AlignVCenter);
    topBarLayout->setAlignment(titleLabel, Qt::AlignVCenter);
    layout->addWidget(topBar);

    contentPanel = new QWidget(this);

    QGridLayout *contentLayout = new QGridLayout(contentPanel);
    contentLayout->setSpacing(0);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    // Left

    directoryList = new QTreeWidget(contentPanel);
    directoryList->setObjectName("leftBrowserPanel");
    directoryList->setMaximumWidth(180);
    directoryList->setHeaderHidden(true);
    directoryList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    connect(directoryList, &QTreeWidget::itemClicked, this, &ImageSourceBrowser::onItemClicked);
    connect(directoryList, &QTreeWidget::itemActivated, this, &ImageSourceBrowser::onItemClicked);

    QGridLayout *directoryListLayout = new QGridLayout(directoryList);
    directoryListLayout->setContentsMargins(0, 0, 2, 0);
    directoryListLayout->setSpacing(0);

    QTreeWidgetItem *topLevelItem = new QTreeWidgetItem(directoryList);
    directoryList->addTopLevelItem(topLevelItem);
    topLevelItem->setText(0, "data");

    QDirIterator it(m_DataPath, QDir::NoDotAndDotDot | QDir::Dirs);
    while (it.hasNext()) {
        QString n = it.next();
        QTreeWidgetItem *item = new QTreeWidgetItem(topLevelItem);
        item->setText(0, n.right(n.length() - n.lastIndexOf("/") - 1));
        recursivelyGetDirectory(n, item);
    }

    topLevelItem->setExpanded(true);

    // Right
    imagesGrid = new QTableWidget(contentPanel);
    imagesGrid->setShowGrid(false);
    imagesGrid->setObjectName("imagesBrowserGrid");

    imagesGrid->horizontalHeader()->setDefaultSectionSize(148);
    imagesGrid->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    imagesGrid->horizontalHeader()->hide();

    imagesGrid->verticalHeader()->setDefaultSectionSize(158);
    imagesGrid->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    imagesGrid->verticalHeader()->hide();

    connect(imagesGrid, &QTableWidget::cellDoubleClicked, this, [this](int row, int column) {
        if(!imagesGrid || m_DataPath.isEmpty())
            return;
        QTableWidgetItem *item = imagesGrid->item(row, column);
        if(!item)
            return;
        const QString absolutePath = item->data(Qt::UserRole).toString();
        if(absolutePath.isEmpty())
            return;
        QString normalizedRoot = QDir::fromNativeSeparators(m_DataPath);
        if(normalizedRoot.endsWith('/'))
            normalizedRoot.chop(1);
        QString normalizedPath = QDir::fromNativeSeparators(absolutePath);
        if(!normalizedPath.startsWith(normalizedRoot))
            return;
        QString relative = normalizedPath.mid(normalizedRoot.length());
        if(relative.isEmpty())
            return;
        if(relative.front() != '/')
            relative.prepend('/');
        emit imageActivated(relative);
    });

    contentLayout->addWidget(directoryList, 0, 0);
    contentLayout->addWidget(imagesGrid, 0, 1);

    layout->addWidget(contentPanel);
}

void ImageSourceBrowser::refresh()
{
    delete directoryList->takeTopLevelItem(0);

    QTreeWidgetItem *topLevelItem = new QTreeWidgetItem(directoryList);
    directoryList->addTopLevelItem(topLevelItem);
    topLevelItem->setText(0, "data");

    QDirIterator it(m_DataPath, QDir::NoDotAndDotDot | QDir::Dirs);
    while (it.hasNext()) {
        QString n = it.next();
        QTreeWidgetItem *item = new QTreeWidgetItem(topLevelItem);
        item->setText(0, n.right(n.length() - n.lastIndexOf("/") - 1));
        recursivelyGetDirectory(n, item);
    }

    topLevelItem->setExpanded(true);
}

void ImageSourceBrowser::handleCloseButton()
{
    this->hide();
}

void ImageSourceBrowser::onItemClicked(QTreeWidgetItem *item, int)
{
    //qDeleteAll(imagesGrid->findChildren<QTableWidgetItem *>(QString(), Qt::FindDirectChildrenOnly));
    imagesGrid->clearContents();
    m_lastCol = 0;
    m_lastRow = 0;

    QString path(m_DataPath + "/");
    if (item->text(0) != "data")
    {
        QTreeWidgetItem *parent = item->parent();
        QStringList parentList;
        while(parent && parent->text(0) != "data")
        {
            parentList.push_front(parent->text(0));
            parent = parent->parent();
        }
        path += parentList.join("/") + "/" + item->text(0);
    }

    QDirIterator it(path, QStringList() << "*.png", QDir::NoDotAndDotDot | QDir::Files);

    imagesGrid->setColumnCount(4);
    imagesGrid->setRowCount(255);
    int items = 0;
    while (it.hasNext()) {
        QString n = it.next();
        addImageToGrid(n.right(n.length() - n.lastIndexOf("/") - 1), n);
        items++;
    }
    const int columnCount = qMin(4, qMax(1, items));
    imagesGrid->setColumnCount(columnCount);
    const int rowCount = items == 0 ? 1 : ((items + columnCount - 1) / columnCount);
    imagesGrid->setRowCount(rowCount);
}

void ImageSourceBrowser::addImageToGrid(QString title, QString path)
{
    QWidget *item = new QWidget;
    item->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    QVBoxLayout *itemLayout = new QVBoxLayout(item);
    itemLayout->setContentsMargins(5, 5, 5, 5);
    itemLayout->setSpacing(4);

    QLabel *imageWidget = new QLabel;
    imageWidget->setMinimumSize(128, 128);
    imageWidget->setAlignment(Qt::AlignCenter);
    QPixmap pic;
    if (!QPixmapCache::find(path, &pic)) {
        pic.load(path);
        QPixmapCache::insert(path, pic);
    }
    if (pic.width() >= pic.height() && pic.width() > 128)
        imageWidget->setPixmap(pic.scaledToWidth(128));
    else if(pic.height() >= pic.width() && pic.height() > 128)
        imageWidget->setPixmap(pic.scaledToHeight(128));
    else
        imageWidget->setPixmap(pic);

    QLabel *imageTitle = new QLabel(title);
    imageTitle->setAlignment(Qt::AlignCenter);

    itemLayout->addWidget(imageWidget);
    itemLayout->addWidget(imageTitle);

    const int targetRow = m_lastRow;
    const int targetColumn = m_lastCol;
    imagesGrid->setCellWidget(targetRow, targetColumn, item);
    auto *tableItem = new QTableWidgetItem;
    tableItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    tableItem->setData(Qt::UserRole, path);
    imagesGrid->setItem(targetRow, targetColumn, tableItem);

    m_lastCol++;
    if(m_lastCol >= 4)
    {
        m_lastRow++;
        m_lastCol = 0;
    }
}

void ImageSourceBrowser::recursivelyGetDirectory(QString path, QTreeWidgetItem *parent)
{
    QDirIterator it(path, QDir::NoDotAndDotDot | QDir::Dirs);
    while (it.hasNext()) {
        QString n = it.next();
        QTreeWidgetItem *item = new QTreeWidgetItem(parent);
        item->setText(0, n.right(n.length() - n.lastIndexOf("/") - 1));
        recursivelyGetDirectory(n, item);
    }
}
