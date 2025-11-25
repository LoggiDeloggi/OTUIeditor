#include "corewindow.h"
#include "ui_mainwindow.h"
#include "startupwindow.h"
#include "modulescanner.h"

#include <QSettings>
#include <QDebug>
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QFileDialog>
#include <QHash>
#include <QtGlobal>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QFileInfo>
#include <QToolButton>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QInputDialog>
#include <QDir>
#include <utility>

namespace {

QString toTitleCase(const QString &raw)
{
    const QStringList parts = raw.split(QRegularExpression("[\\s_\\-]+"), Qt::SkipEmptyParts);
    QStringList formatted;
    formatted.reserve(parts.size());
    for(QString part : parts)
    {
        part = part.toLower();
        if(!part.isEmpty())
            part[0] = part[0].toUpper();
        formatted.push_back(part);
    }
    if(formatted.isEmpty())
        return QStringLiteral("Other");
    return formatted.join(' ');
}

QString deriveTemplateCategory(const QString &filePath)
{
    const QFileInfo info(filePath);
    QString baseName = info.completeBaseName();
    const int dashPos = baseName.indexOf('-');
    if(dashPos >= 0)
        baseName = baseName.mid(dashPos + 1);
    if(baseName.isEmpty())
        baseName = info.fileName();
    return toTitleCase(baseName);
}

QString anchorEdgeToken(OTUI::AnchorEdge edge)
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

class ModuleResourceScope
{
public:
    explicit ModuleResourceScope(const QString &filePath)
    {
        const QFileInfo info(filePath);
        const QString dirPath = QDir::fromNativeSeparators(info.absolutePath());
        const QString lowered = dirPath.toLower();
        const QString token = QStringLiteral("/modules/");
        const int idx = lowered.lastIndexOf(token);
        if(idx < 0)
            return;

        const int moduleNameStart = idx + token.size();
        const int nextSlash = dirPath.indexOf('/', moduleNameStart);
        QString moduleDirPath;
        if(nextSlash < 0)
            moduleDirPath = dirPath;
        else
            moduleDirPath = dirPath.left(nextSlash);

        const QString modulesRoot = dirPath.left(idx);
        OTUI::setModulesRootPath(modulesRoot);
        OTUI::setModuleAssetsRoot(moduleDirPath);
        m_active = true;
    }

    ~ModuleResourceScope()
    {
        if(!m_active)
            return;
        OTUI::setModulesRootPath(QString());
        OTUI::setModuleAssetsRoot(QString());
    }

private:
    bool m_active = false;
};

} // namespace

CoreWindow::CoreWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
}

CoreWindow::~CoreWindow()
{
    delete std::exchange(imagesBrowser, nullptr);
    delete std::exchange(stylesBrowser, nullptr);
    delete std::exchange(m_projectSettings, nullptr);
    delete ui;
}

void CoreWindow::initializeWindow()
{
    setMinimumSize(860, 600);
    ui->setupUi(this);

    const auto initAnchorCombo = [](QComboBox *combo) {
        if(!combo)
            return;
        combo->clear();
        combo->addItem("Parent", QStringLiteral("parent"));
        combo->addItem("Previous", QStringLiteral("prev"));
        combo->addItem("Custom", QString());
        combo->setCurrentIndex(0);
    };

    initAnchorCombo(ui->anchorLeftTargetCombo);
    initAnchorCombo(ui->anchorRightTargetCombo);
    initAnchorCombo(ui->anchorTopTargetCombo);
    initAnchorCombo(ui->anchorBottomTargetCombo);
    initAnchorCombo(ui->anchorHCenterTargetCombo);
    initAnchorCombo(ui->anchorVCenterTargetCombo);

    model = new QStandardItemModel;
    model->setHeaderData(0, Qt::Horizontal, "Widgets List");
    ui->treeView->setModel(model);
    connect(ui->treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [=](const QItemSelection &selected, const QItemSelection&) {
        if(selected.indexes().isEmpty()) {
            m_selected = nullptr;
            updatePropertyPanel(nullptr);
            return;
        }
        QStandardItem *newitem = model->itemFromIndex(selected.indexes().first());
        m_selected = nullptr;
        selectWidgetById(newitem->text());

        if(m_selected != nullptr) {
            ui->openGLWidget->m_selected = m_selected;
        }
        updatePropertyPanel(m_selected);
    });

    connect(ui->openGLWidget, &OpenGLWidget::selectionChanged, this, [this](OTUI::Widget *widget) {
        if(m_selected == widget)
            return;
        m_selected = widget;
        if(widget)
            syncTreeSelection(widget);
        else if(ui->treeView->selectionModel())
            ui->treeView->clearSelection();
        updatePropertyPanel(widget);
    });

    connect(ui->openGLWidget, &OpenGLWidget::widgetGeometryChanged, this, [this](OTUI::Widget *widget) {
        if(widget != m_selected)
            return;
        updatePropertyPanel(widget);
        setProjectChanged(true);
    });

    imagesBrowser = new ImageSourceBrowser(ui->centralWidget);
    imagesBrowser->hide();
    connect(imagesBrowser, &ImageSourceBrowser::imageActivated, this, &CoreWindow::handleImageSelection);

    stylesBrowser = new StyleSourceBrowser(ui->centralWidget);
    stylesBrowser->hide();
    connect(stylesBrowser, &StyleSourceBrowser::styleActivated, this, [this](const QString &filePath) {
        importOtuiFile(filePath);
        stylesBrowser->hide();
    });
    connect(stylesBrowser, &StyleSourceBrowser::styleTemplateActivated, this, &CoreWindow::handleStyleTemplateActivated);

    initializePropertyPanel();

    m_projectSettings = new ProjectSettings(this);
    m_projectSettings->hide();
}

void CoreWindow::ShowError(QString title, QString description)
{
    QMessageBox messageBox;
    messageBox.critical(nullptr, title, description);
    messageBox.setFixedSize(300, 80);
}

void CoreWindow::startNewProject(QString fileName, QString name, QString path, QString dataPath)
{
    m_Project = new OTUI::Project(fileName, name, path, dataPath);

    if(!m_Project->loaded())
        return;

    initializeWindow();
    setWindowTitle(name + " - OTUI Editor");
    m_projectSettings->setProjectName(name);
    m_projectSettings->setDataPath(dataPath);
    imagesBrowser->m_DataPath = m_Project->getDataPath();
    imagesBrowser->initialize();
    if(stylesBrowser)
    {
        stylesBrowser->setDataPath(m_Project->getDataPath());
        stylesBrowser->initialize();
    }
}

void CoreWindow::loadProjectData(QDataStream &data, QString fileName, QString path)
{
    m_Project = new OTUI::Project(data, fileName, path);

    if(!m_Project->loaded())
        return;

    initializeWindow();
    // TODO: Initialize widgets

    setWindowTitle(m_Project->getProjectName() + " - OTUI Editor");
    m_projectSettings->setProjectName(m_Project->getProjectName());
    m_projectSettings->setDataPath(m_Project->getDataPath());
    imagesBrowser->m_DataPath = m_Project->getDataPath();
    imagesBrowser->initialize();
    if(stylesBrowser)
    {
        stylesBrowser->setDataPath(m_Project->getDataPath());
        stylesBrowser->initialize();
    }
}

bool CoreWindow::event(QEvent *event)
{
    if(event->type() == SetIdEvent::eventType)
    {
        SetIdEvent *setIdEvent = reinterpret_cast<SetIdEvent*>(event);
        QModelIndexList items = model->match(model->index(0, 0), Qt::DisplayRole, QVariant::fromValue(setIdEvent->oldId), 1, Qt::MatchRecursive);
        if(!items.isEmpty())
        {
            model->itemFromIndex(items.at(0))->setText(setIdEvent->newId);
            setProjectChanged(true);
        }
    }
    else if(event->type() == SettingsSavedEvent::eventType)
    {
        m_Project->setProjectName(m_projectSettings->getProjectName());
        m_Project->setDataPath(m_projectSettings->getDataPath());
        imagesBrowser->m_DataPath = m_Project->getDataPath();
        imagesBrowser->refresh();
        if(stylesBrowser)
        {
            stylesBrowser->setDataPath(m_Project->getDataPath());
            stylesBrowser->refresh();
        }
        setProjectChanged(true);

        ui->openGLWidget->sendEvent(event);
    }

    return QMainWindow::event(event);
}

void CoreWindow::resizeEvent(QResizeEvent*)
{
    if(isHidden()) return;

    if(imagesBrowser && imagesBrowser->isVisible())
        imagesBrowser->move(this->rect().center() - imagesBrowser->rect().center());

    if(m_projectSettings && m_projectSettings->isVisible())
        m_projectSettings->move(this->rect().center() - m_projectSettings->rect().center());
}

bool CoreWindow::eventFilter(QObject*, QEvent*)
{
    return false;
}

void CoreWindow::keyReleaseEvent(QKeyEvent *event)
{
    switch(event->key())
    {

    case Qt::Key_Delete:
    {
        auto idx = ui->treeView->currentIndex();
        if(idx.parent().isValid())
        {
            QStandardItem *item = model->itemFromIndex(idx);
            ui->openGLWidget->deleteWidget(item->text());
            model->removeRow(idx.row(), idx.parent());
            QStandardItem *root = model->invisibleRootItem();
            QModelIndex index = root->child(0)->index();
            ui->treeView->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            ui->treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
            selectWidgetById(root->child(0)->text());
        }
        else
        {
            model->clear();
            ui->openGLWidget->clearWidgets();
            m_selected = nullptr;
        }
        setProjectChanged(true);
        break;
    }

    }
}

void CoreWindow::closeEvent(QCloseEvent *event)
{
    if(!m_Project->isChanged())
    {
        event->accept();
        return;
    }

    QMessageBox box;
    QMessageBox::StandardButton response = box.question(this, "Save Changes",
                 "Do you want to save this project before closing?",
                 QMessageBox::Cancel | QMessageBox::No | QMessageBox::Yes,
                 QMessageBox::Yes);

    if(response == QMessageBox::Yes)
    {
        if(m_Project->save())
            event->accept();
        else
            event->ignore();
    }
    else if(response == QMessageBox::No)
        event->accept();
    else
        event->ignore();
}

void CoreWindow::addChildToTree(QString label)
{
    QModelIndex index = ui->treeView->currentIndex();
    QStandardItem *item = model->itemFromIndex(index);
    QStandardItem *newItem = new QStandardItem(label);
    newItem->setEditable(false);
    item->appendRow(newItem);
    ui->treeView->expand(index);
    ui->treeView->selectionModel()->select(newItem->index(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    ui->treeView->selectionModel()->setCurrentIndex(newItem->index(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    selectWidgetById(newItem->text());
    setProjectChanged(true);
}

void CoreWindow::selectWidgetById(QString widgetId)
{
    m_selected = findWidgetById(widgetId);
}

void CoreWindow::on_treeView_customContextMenuRequested(const QPoint &pos)
{
    QMenu menu(this);
    ui->actionDeleteWidget->setEnabled(ui->treeView->indexAt(pos).isValid());

    menu.addAction(ui->actionDeleteWidget);

    menu.addSeparator();

    QMenu *newMenu = menu.addMenu("New...");

    newMenu->addAction(ui->newMainWindow);
    newMenu->addAction(ui->newButton);
    newMenu->addAction(ui->newLabel);
    newMenu->addAction(ui->newUIItem);
    newMenu->addAction(ui->newUICreature);
    newMenu->addSeparator();

    QMenu *customMenu = newMenu->addMenu("Custom");
    bool hasTemplates = false;
    if(stylesBrowser)
    {
        const auto &templates = stylesBrowser->styleTemplates();
        if(!templates.isEmpty())
        {
            QMap<QString, QList<const StyleSourceBrowser::StyleTemplateEntry*>> grouped;
            for(const auto &entry : templates)
            {
                const QString category = deriveTemplateCategory(entry.filePath);
                grouped[category].append(&entry);
            }

            hasTemplates = !grouped.isEmpty();
            for(auto it = grouped.cbegin(); it != grouped.cend(); ++it)
            {
                QMenu *categoryMenu = customMenu->addMenu(it.key());
                for(const auto *entryPtr : it.value())
                {
                    const auto &entry = *entryPtr;
                    QAction *action = categoryMenu->addAction(entry.displayName);
                    connect(action, &QAction::triggered, this, [this, filePath = entry.filePath, styleName = entry.styleName]() {
                        instantiateStyleIntoSelection(filePath, styleName);
                    });
                }
            }

            if(hasTemplates)
                customMenu->addSeparator();
        }
    }

    QAction *browseStyles = customMenu->addAction("Browse Styles...");
    connect(browseStyles, &QAction::triggered, this, &CoreWindow::showStylesBrowser);

    customMenu->setEnabled(hasTemplates || stylesBrowser);

    menu.exec(ui->treeView->mapToGlobal(pos));
}

void CoreWindow::on_actionDeleteWidget_triggered()
{
    auto idx = ui->treeView->currentIndex();
    if(idx.parent().isValid())
    {
        QStandardItem *item = model->itemFromIndex(idx);
        ui->openGLWidget->deleteWidget(item->text());
        model->removeRow(idx.row(), idx.parent());
        QStandardItem *root = model->invisibleRootItem();
        QModelIndex index = root->child(0)->index();
        ui->treeView->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        ui->treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        selectWidgetById(root->child(0)->text());
    }
    else
    {
        model->clear();
        ui->openGLWidget->clearWidgets();
        m_selected = nullptr;
    }
    setProjectChanged(true);
}

void CoreWindow::on_actionNewProject_triggered()
{
    if(m_Project->isChanged())
    {
        QMessageBox box;
        QMessageBox::StandardButton response = box.question(this, "Save Changes",
                     "Do you want to save this project before closing?",
                     QMessageBox::Cancel | QMessageBox::No | QMessageBox::Yes,
                     QMessageBox::Yes);

        if(response == QMessageBox::Yes)
        {
            if(!m_Project->save())
                return;
        }
        else if(response == QMessageBox::Cancel)
            return;
    }

    if(m_Project->getProjectFile()->isOpen())
    {
        m_Project->getProjectFile()->close();
    }

    // Clear tree
    model->clear();

    // Clear selected
    m_selected = nullptr;

    // Clear widgets
    ui->openGLWidget->clearWidgets();
}

void CoreWindow::on_actionOpenProject_triggered()
{
    QString startDir = m_Project ? m_Project->getProjectPath() : QDir::homePath();
    if(m_Project)
    {
        QDir dataDir(m_Project->getDataPath());
        const QString stylesDir = dataDir.filePath("styles");
        if(QDir(stylesDir).exists())
            startDir = stylesDir;
        else if(dataDir.exists())
            startDir = dataDir.absolutePath();
    }
    const QString filePath = QFileDialog::getOpenFileName(this,
                                                         "Open OTUI File",
                                                         startDir,
                                                         "OTUI Files (*.otui);;All Files (*)");
    if(filePath.isEmpty())
        return;

    importOtuiFile(filePath);
}

void CoreWindow::on_actionOpenModule_triggered()
{
    if(!m_Project)
    {
        ShowError("Module Import", "Open a project before importing modules.");
        return;
    }

    QString startDir = m_Project->getProjectPath();
    if(startDir.isEmpty())
        startDir = QDir::homePath();
    QDir projectDir(startDir);
    if(projectDir.exists("modules"))
        startDir = projectDir.filePath("modules");

    const QString otmodPath = QFileDialog::getOpenFileName(this,
                                                          "Open Module",
                                                          startDir,
                                                          "OTClient Modules (*.otmod);;All Files (*)");
    if(otmodPath.isEmpty())
        return;

    ModuleScanner scanner;
    ModuleScanner::Result result;
    QString error;
    const QString dataPathHint = m_Project ? m_Project->getDataPath() : QString();
    if(!scanner.scan(otmodPath, dataPathHint, result, &error))
    {
        ShowError("Module Import", error.isEmpty() ? QStringLiteral("Failed to scan the selected module.") : error);
        return;
    }

    if(result.entries.isEmpty())
    {
        ShowError("Module Import", "No UI files were found in the selected module.");
        return;
    }

    if(!result.missingUiFiles.isEmpty())
    {
        QMessageBox::information(this,
                                 "Module Warnings",
                                 QStringLiteral("Some referenced UI files were not found:\n%1")
                                     .arg(result.missingUiFiles.join(QLatin1Char('\n'))));
    }

    int selectedIndex = 0;
    if(result.entries.size() > 1)
    {
        QStringList labels;
        labels.reserve(result.entries.size());
        for(const auto &entry : result.entries)
            labels << entry.label;

        const int defaultIndex = qBound(0, result.primaryIndex, labels.size() - 1);
        bool ok = false;
        const QString choice = QInputDialog::getItem(this,
                                                     QStringLiteral("%1 - Select UI").arg(result.moduleName),
                                                     "Select an interface to open:",
                                                     labels,
                                                     defaultIndex,
                                                     false,
                                                     &ok);
        if(!ok)
            return;

        selectedIndex = labels.indexOf(choice);
        if(selectedIndex < 0)
            selectedIndex = defaultIndex;
    }

    const QString targetPath = result.entries.at(selectedIndex).absolutePath;
    importOtuiFile(targetPath);
}

void CoreWindow::on_actionSaveProject_triggered()
{
    if(m_Project->save())
        setWindowTitle(m_Project->getProjectName() + " - OTUI Editor");
}

void CoreWindow::on_actionCloseProject_triggered()
{
    if(m_Project->isChanged())
    {
        QMessageBox box;
        QMessageBox::StandardButton response = box.question(this, "Save Changes",
                     "Do you want to save this project before closing?",
                     QMessageBox::Cancel | QMessageBox::No | QMessageBox::Yes,
                     QMessageBox::Yes);

        if(response == QMessageBox::Yes)
        {
            if(!m_Project->save())
                return;
        }
        else if(response == QMessageBox::Cancel)
            return;
    }

    if(m_Project)
    {
        m_Project->getProjectFile()->close();
    }

    StartupWindow *w = new StartupWindow();
    w->show();
    hide();

}

void CoreWindow::on_horizontalSlider_valueChanged(int value)
{
    ui->openGLWidget->scale = value / 100.0;
    ui->zoomLabel->setText(QString::number(value) + "%");
}

void CoreWindow::on_newMainWindow_triggered()
{
    QModelIndex index = ui->treeView->currentIndex();
    QString widgetId("mainWindow");
    if(index.isValid())
    {
        addChildToTree(widgetId);
    }
    else
    {
        QStandardItem *root = model->invisibleRootItem();
        QStandardItem *item = new QStandardItem(widgetId);
        item->setEditable(false);
        root->appendRow(item);
        model->setHeaderData(0, Qt::Horizontal, "Widgets List");
        ui->treeView->selectionModel()->select(item->index(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        ui->treeView->selectionModel()->setCurrentIndex(item->index(), QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        selectWidgetById(item->text());
    }

    m_selected = ui->openGLWidget->addWidget<OTUI::MainWindow>(widgetId,
                                                               m_Project->getDataPath(),
                                                               "/images/ui/window.png",
                                                               QRect(6, 27, 6, 6));
    setProjectChanged(true);
}

void CoreWindow::on_newButton_triggered()
{
    QModelIndex index = ui->treeView->currentIndex();
    if(index.isValid())
    {
        QString widgetId("button");
        m_selected = ui->openGLWidget->addWidgetChild<OTUI::Button>("mainWindow",
                                         widgetId,
                                         m_Project->getDataPath(),
                                         "/images/ui/button_rounded.png",
                                         QRect(0, 0, 22, 23),
                                         QRect(5, 5, 5, 5));
        addChildToTree(m_selected->getId());
        setProjectChanged(true);
    }
}

void CoreWindow::on_newLabel_triggered()
{
    QModelIndex index = ui->treeView->currentIndex();
    if(index.isValid())
    {
        QString widgetId("label");
        m_selected = ui->openGLWidget->addWidgetChild<OTUI::Label>("mainWindow",
                                        widgetId,
                                        m_Project->getDataPath(),
                                        "",
                                        QRect(0, 0, 0, 0),
                                        QRect(0, 0, 0, 0));
        addChildToTree(m_selected->getId());
        setProjectChanged(true);
    }
}

void CoreWindow::on_newUIItem_triggered()
{
    QModelIndex index = ui->treeView->currentIndex();
    if(index.isValid())
    {
        QString widgetId("item");
        m_selected = ui->openGLWidget->addWidgetChild<OTUI::Item>("mainWindow",
                                       widgetId,
                                       m_Project->getDataPath(),
                                       "",
                                       QRect(0, 0, 0, 0),
                                       QRect(0, 0, 0, 0));
        addChildToTree(m_selected->getId());
        setProjectChanged(true);
    }
}

void CoreWindow::on_newUICreature_triggered()
{
    QModelIndex index = ui->treeView->currentIndex();
    if(index.isValid())
    {
        QString widgetId("creature");
        m_selected = ui->openGLWidget->addWidgetChild<OTUI::Creature>("mainWindow",
                                           widgetId,
                                           m_Project->getDataPath(),
                                           "",
                                           QRect(0, 0, 0, 0),
                                           QRect(0, 0, 0, 0));
        addChildToTree(m_selected->getId());
        setProjectChanged(true);
    }
}

void CoreWindow::on_newImage_triggered()
{
    QModelIndex index = ui->treeView->currentIndex();
    if(!index.isValid())
        return;

    QString widgetId("image");
    m_selected = ui->openGLWidget->addWidgetChild<OTUI::Image>("mainWindow",
                                       widgetId,
                                       m_Project->getDataPath(),
                                       "",
                                       QRect(0, 0, 0, 0),
                                       QRect(0, 0, 0, 0));
    addChildToTree(m_selected->getId());
    setProjectChanged(true);
}

void CoreWindow::on_actionProject_Settings_triggered()
{
    m_projectSettings->move(this->rect().center() - m_projectSettings->rect().center());
    m_projectSettings->show();
}

void CoreWindow::setProjectChanged(bool v) {
    if(!m_Project)
        return;

    if(v)
        setWindowTitle(m_Project->getProjectName() + " * - OTUI Editor");
    else
        setWindowTitle(m_Project->getProjectName() + " - OTUI Editor");
    m_Project->setChanged(v);
}

bool CoreWindow::importOtuiFile(const QString &filePath, const QString &dataPathOverride)
{
    ModuleResourceScope moduleScope(filePath);
    OTUI::Parser::WidgetList widgets;
    QString error;
    QString dataPath = dataPathOverride;
    if(dataPath.isEmpty() && m_Project)
        dataPath = m_Project->getDataPath();
    if(!m_parser.loadFromFile(filePath, widgets, &error, dataPath))
    {
        ShowError("Parser Error", error.isEmpty() ? QStringLiteral("Unknown parser error.") : error);
        return false;
    }

    ui->openGLWidget->setWidgets(std::move(widgets));
    rebuildWidgetTree();
    if(m_Project)
        setProjectChanged(true);
    m_currentOtuiPath = filePath;
    return true;
}

void CoreWindow::rebuildWidgetTree()
{
    if(!model)
        return;

    model->clear();
    model->setHeaderData(0, Qt::Horizontal, "Widgets List");

    const auto &widgets = ui->openGLWidget->getOTUIWidgets();

    if(widgets.empty())
        return;

    QHash<QString, QStandardItem*> itemsById;
    itemsById.reserve(static_cast<int>(widgets.size()));

    for(const auto &widget : widgets)
    {
        if(!widget)
            continue;
        auto *item = new QStandardItem(widget->getId());
        item->setEditable(false);
        itemsById.insert(widget->getId(), item);
    }

    QStandardItem *root = model->invisibleRootItem();
    for(const auto &widget : widgets)
    {
        if(!widget)
            continue;
        QStandardItem *item = itemsById.value(widget->getId(), nullptr);
        if(!item)
            continue;

        OTUI::Widget *parentWidget = widget->getParent();
        if(parentWidget)
        {
            QStandardItem *parentItem = itemsById.value(parentWidget->getId(), nullptr);
            if(parentItem)
                parentItem->appendRow(item);
            else
                root->appendRow(item);
        }
        else
        {
            root->appendRow(item);
        }
    }

    if(model->rowCount() == 0)
        return;

    QString desiredId;
    if(m_selected)
        desiredId = m_selected->getId();

    QStandardItem *fallbackItem = root->child(0);
    QModelIndex targetIndex;
    if(!desiredId.isEmpty())
    {
        const QModelIndexList matches = model->match(model->index(0, 0), Qt::DisplayRole, desiredId, 1, Qt::MatchRecursive);
        if(!matches.isEmpty())
            targetIndex = matches.first();
    }

    if(!targetIndex.isValid() && fallbackItem)
        targetIndex = fallbackItem->index();

    if(!targetIndex.isValid())
        return;

    ui->treeView->selectionModel()->select(targetIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    ui->treeView->selectionModel()->setCurrentIndex(targetIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QStandardItem *item = model->itemFromIndex(targetIndex);
    if(item)
        selectWidgetById(item->text());
    ui->openGLWidget->m_selected = m_selected;
}

void CoreWindow::initializePropertyPanel()
{
    if(!ui)
        return;

    if(!m_imageSourceLabel)
    {
        if(auto *layout = ui->ispContent ? ui->ispContent->layout() : nullptr)
        {
            m_imageSourceLabel = new ElidedLabel(QString(), ui->ispContent);
            m_imageSourceLabel->setReadOnly(true);
            layout->addWidget(m_imageSourceLabel);

            m_imageBrowseButton = new QPushButton("Browse", ui->ispContent);
            m_imageBrowseButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            layout->addWidget(m_imageBrowseButton);
            connect(m_imageBrowseButton, &QPushButton::clicked, this, [this]() {
                if(!imagesBrowser)
                    return;
                imagesBrowser->move(this->rect().center() - imagesBrowser->rect().center());
                imagesBrowser->show();
                imagesBrowser->raise();
            });
        }
    }

    auto setupSectionToggle = [](QToolButton *button, QWidget *content) {
        if(!button || !content)
            return;
        button->setCheckable(true);
        if(!button->isChecked())
            content->hide();
        QObject::connect(button, &QToolButton::toggled, content, [button, content](bool checked) {
            content->setVisible(checked);
            button->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        });
        button->setArrowType(button->isChecked() ? Qt::DownArrow : Qt::RightArrow);
    };

    setupSectionToggle(ui->pushButton, ui->content);
    setupSectionToggle(ui->pushButton_2, ui->content_2);

    connectPropertyEditors();
    setPropertyEditorsEnabled(false);
}

void CoreWindow::connectPropertyEditors()
{
    connect(ui->widgetIdLineEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_updatingProperties || !m_selected)
            return;
        const QString newId = ui->widgetIdLineEdit->text().trimmed();
        if(newId.isEmpty() || newId == m_selected->getId())
            return;
        m_selected->setIdProperty(newId);
        syncTreeSelection(m_selected);
        setProjectChanged(true);
    });

    connect(ui->widgetTextLineEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_updatingProperties || !m_selected || !m_selected->supportsTextProperty())
            return;
        const QString newText = ui->widgetTextLineEdit->text();
        if(newText == m_selected->textProperty())
            return;
        m_selected->setTextProperty(newText);
        ui->openGLWidget->update();
        setProjectChanged(true);
    });

    auto connectSpin = [this](QSpinBox *spin, auto updater) {
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, [this, updater](int value) {
            if(m_updatingProperties || !m_selected)
                return;
            updater(value);
            ui->openGLWidget->update();
            setProjectChanged(true);
        });
    };

    connectSpin(ui->posXSpin, [this](int value) {
        QPoint pos = m_selected->getPos();
        pos.setX(value);
        m_selected->setPos(pos);
    });

    connectSpin(ui->posYSpin, [this](int value) {
        QPoint pos = m_selected->getPos();
        pos.setY(value);
        m_selected->setPos(pos);
    });

    connectSpin(ui->widthSpin, [this](int value) {
        QRect rect = *m_selected->getRect();
        rect.setWidth(value);
        m_selected->setRect(rect);
    });

    connectSpin(ui->heightSpin, [this](int value) {
        QRect rect = *m_selected->getRect();
        rect.setHeight(value);
        m_selected->setRect(rect);
    });

    connect(ui->opacitySpin, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if(m_updatingProperties || !m_selected)
            return;
        m_selected->setOpacity(static_cast<float>(value));
        ui->openGLWidget->update();
        setProjectChanged(true);
    });

    connect(ui->visibleCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if(m_updatingProperties || !m_selected)
            return;
        m_selected->setVisibleProperty(checked);
        ui->openGLWidget->update();
        setProjectChanged(true);
    });

    auto borderUpdater = [this](int) {
        if(m_updatingProperties || !m_selected)
            return;
        QRect border = m_selected->getImageBorder();
        border.setX(ui->borderLeftSpin->value());
        border.setY(ui->borderTopSpin->value());
        border.setWidth(ui->borderRightSpin->value());
        border.setHeight(ui->borderBottomSpin->value());
        m_selected->setImageBorder(border);
        ui->openGLWidget->update();
        setProjectChanged(true);
    };

    for(QSpinBox *spin : {ui->borderLeftSpin, ui->borderTopSpin, ui->borderRightSpin, ui->borderBottomSpin})
    {
        connect(spin, qOverload<int>(&QSpinBox::valueChanged), this, borderUpdater);
    }

    connect(ui->phantomCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if(m_updatingProperties || !m_selected)
            return;
        m_selected->setPhantom(checked);
        ui->openGLWidget->update();
        setProjectChanged(true);
    });

    connect(ui->colorLineEdit, &QLineEdit::editingFinished, this, [this]() {
        if(m_updatingProperties || !m_selected)
            return;
        const QString text = ui->colorLineEdit->text().trimmed();
        if(text.isEmpty())
        {
            if(m_selected->colorString().isEmpty())
                return;
            m_selected->setColor(QColor());
            ui->openGLWidget->update();
            setProjectChanged(true);
            return;
        }

        QColor color(text);
        if(!color.isValid())
        {
            updatePropertyPanel(m_selected);
            return;
        }
        if(color == m_selected->getColor())
            return;
        m_selected->setColor(color);
        ui->openGLWidget->update();
        setProjectChanged(true);
    });

    connectSpin(ui->marginTopSpin, [this](int value) {
        m_selected->setMarginTop(value);
        applyAnchorsForWidget(m_selected);
    });
    connectSpin(ui->marginRightSpin, [this](int value) {
        m_selected->setMarginRight(value);
        applyAnchorsForWidget(m_selected);
    });
    connectSpin(ui->marginBottomSpin, [this](int value) {
        m_selected->setMarginBottom(value);
        applyAnchorsForWidget(m_selected);
    });
    connectSpin(ui->marginLeftSpin, [this](int value) {
        m_selected->setMarginLeft(value);
        applyAnchorsForWidget(m_selected);
    });

    connectSpin(ui->paddingTopSpin, [this](int value) {
        m_selected->setPaddingTop(value);
    });
    connectSpin(ui->paddingRightSpin, [this](int value) {
        m_selected->setPaddingRight(value);
    });
    connectSpin(ui->paddingBottomSpin, [this](int value) {
        m_selected->setPaddingBottom(value);
    });
    connectSpin(ui->paddingLeftSpin, [this](int value) {
        m_selected->setPaddingLeft(value);
    });

    struct AnchorControl {
        QCheckBox *check;
        QComboBox *target;
        QLineEdit *custom;
        OTUI::AnchorEdge edge;
    };

    auto refreshCustomState = [](const AnchorControl &ctrl) {
        if(!ctrl.target || !ctrl.custom)
            return;
        const bool custom = ctrl.check && ctrl.check->isChecked() && ctrl.target->currentIndex() == ctrl.target->count() - 1;
        ctrl.custom->setEnabled(custom);
    };

    auto applyAnchorState = [this](const AnchorControl &ctrl) {
        if(!ctrl.check || !ctrl.target || !ctrl.custom)
            return;
        if(m_updatingProperties || !m_selected)
            return;

        if(!ctrl.check->isChecked())
        {
            m_selected->clearAnchorBinding(ctrl.edge);
        }
        else
        {
            QString targetId;
            if(ctrl.target->currentIndex() == ctrl.target->count() - 1)
                targetId = ctrl.custom->text().trimmed();
            else
                targetId = ctrl.target->currentData().toString();

            if(targetId.isEmpty())
            {
                m_selected->clearAnchorBinding(ctrl.edge);
            }
            else
            {
                const QString token = anchorEdgeToken(ctrl.edge);
                if(!token.isEmpty())
                    m_selected->setAnchorFromDescriptor(ctrl.edge, QStringLiteral("%1.%2").arg(targetId, token));
            }
        }

        applyAnchorsForWidget(m_selected);
        ui->openGLWidget->update();
        setProjectChanged(true);
    };

    auto connectAnchorControl = [&](const AnchorControl &ctrl) {
        if(!ctrl.check || !ctrl.target || !ctrl.custom)
            return;

        refreshCustomState(ctrl);

        connect(ctrl.check, &QCheckBox::toggled, this, [this, ctrl, applyAnchorState, refreshCustomState](bool) {
            refreshCustomState(ctrl);
            if(m_updatingProperties || !m_selected)
                return;
            applyAnchorState(ctrl);
        });

        connect(ctrl.target, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, ctrl, applyAnchorState, refreshCustomState](int) {
            refreshCustomState(ctrl);
            if(m_updatingProperties || !m_selected)
                return;
            if(ctrl.check && ctrl.check->isChecked())
                applyAnchorState(ctrl);
        });

        connect(ctrl.custom, &QLineEdit::editingFinished, this, [this, ctrl, applyAnchorState]() {
            if(m_updatingProperties || !m_selected)
                return;
            if(!ctrl.target || ctrl.target->currentIndex() != ctrl.target->count() - 1)
                return;
            if(ctrl.check && !ctrl.check->isChecked())
                return;
            applyAnchorState(ctrl);
        });
    };

    const QVector<AnchorControl> anchorControls = {
        {ui->anchorLeftCheckBox, ui->anchorLeftTargetCombo, ui->anchorLeftCustomLineEdit, OTUI::AnchorEdge::Left},
        {ui->anchorRightCheckBox, ui->anchorRightTargetCombo, ui->anchorRightCustomLineEdit, OTUI::AnchorEdge::Right},
        {ui->anchorTopCheckBox, ui->anchorTopTargetCombo, ui->anchorTopCustomLineEdit, OTUI::AnchorEdge::Top},
        {ui->anchorBottomCheckBox, ui->anchorBottomTargetCombo, ui->anchorBottomCustomLineEdit, OTUI::AnchorEdge::Bottom},
        {ui->anchorHCenterCheckBox, ui->anchorHCenterTargetCombo, ui->anchorHCenterCustomLineEdit, OTUI::AnchorEdge::HorizontalCenter},
        {ui->anchorVCenterCheckBox, ui->anchorVCenterTargetCombo, ui->anchorVCenterCustomLineEdit, OTUI::AnchorEdge::VerticalCenter}
    };

    for(const AnchorControl &ctrl : anchorControls)
        connectAnchorControl(ctrl);

    auto connectAnchorTarget = [this](QLineEdit *lineEdit, auto updater) {
        if(!lineEdit)
            return;
        connect(lineEdit, &QLineEdit::editingFinished, this, [this, lineEdit, updater]() {
            if(m_updatingProperties || !m_selected)
                return;
            if(!updater(lineEdit->text()))
            {
                updatePropertyPanel(m_selected);
                return;
            }
            applyAnchorsForWidget(m_selected);
            ui->openGLWidget->update();
            setProjectChanged(true);
        });
    };

    connectAnchorTarget(ui->anchorCenterInLineEdit, [this](const QString &value) {
        const QString trimmed = value.trimmed();
        if(m_selected->centerInTarget() == trimmed)
            return false;
        m_selected->setCenterInTarget(trimmed);
        return true;
    });

    connectAnchorTarget(ui->anchorFillLineEdit, [this](const QString &value) {
        const QString trimmed = value.trimmed();
        if(m_selected->fillTarget() == trimmed)
            return false;
        m_selected->setFillTarget(trimmed);
        return true;
    });
}

void CoreWindow::setPropertyEditorsEnabled(bool enabled)
{
    const QList<QWidget*> editors = {
        ui->widgetIdLineEdit,
        ui->widgetTextLineEdit,
        ui->posXSpin,
        ui->posYSpin,
        ui->widthSpin,
        ui->heightSpin,
        ui->opacitySpin,
        ui->visibleCheckBox,
        ui->borderLeftSpin,
        ui->borderTopSpin,
        ui->borderRightSpin,
        ui->borderBottomSpin,
        ui->phantomCheckBox,
        ui->colorLineEdit,
        ui->marginTopSpin,
        ui->marginRightSpin,
        ui->marginBottomSpin,
        ui->marginLeftSpin,
        ui->paddingTopSpin,
        ui->paddingRightSpin,
        ui->paddingBottomSpin,
        ui->paddingLeftSpin,
        ui->anchorLeftCheckBox,
        ui->anchorLeftTargetCombo,
        ui->anchorLeftCustomLineEdit,
        ui->anchorRightCheckBox,
        ui->anchorRightTargetCombo,
        ui->anchorRightCustomLineEdit,
        ui->anchorTopCheckBox,
        ui->anchorTopTargetCombo,
        ui->anchorTopCustomLineEdit,
        ui->anchorBottomCheckBox,
        ui->anchorBottomTargetCombo,
        ui->anchorBottomCustomLineEdit,
        ui->anchorHCenterCheckBox,
        ui->anchorHCenterTargetCombo,
        ui->anchorHCenterCustomLineEdit,
        ui->anchorVCenterCheckBox,
        ui->anchorVCenterTargetCombo,
        ui->anchorVCenterCustomLineEdit,
        ui->anchorCenterInLineEdit,
        ui->anchorFillLineEdit
    };

    for(QWidget *editor : editors)
    {
        if(editor)
            editor->setEnabled(enabled);
    }

    if(m_imageSourceLabel)
        m_imageSourceLabel->setEnabled(enabled);
    if(m_imageBrowseButton)
        m_imageBrowseButton->setEnabled(enabled);
}

void CoreWindow::updatePropertyPanel(OTUI::Widget *widget)
{
    m_updatingProperties = true;

    if(!widget)
    {
        ui->widgetIdLineEdit->clear();
        if(ui->widgetTextLineEdit)
            ui->widgetTextLineEdit->clear();
        ui->posXSpin->setValue(0);
        ui->posYSpin->setValue(0);
        ui->widthSpin->setValue(1);
        ui->heightSpin->setValue(1);
        ui->opacitySpin->setValue(1.0);
        ui->visibleCheckBox->setChecked(true);
        ui->borderLeftSpin->setValue(0);
        ui->borderTopSpin->setValue(0);
        ui->borderRightSpin->setValue(0);
        ui->borderBottomSpin->setValue(0);
        ui->phantomCheckBox->setChecked(false);
        ui->colorLineEdit->clear();
        ui->marginTopSpin->setValue(0);
        ui->marginRightSpin->setValue(0);
        ui->marginBottomSpin->setValue(0);
        ui->marginLeftSpin->setValue(0);
        ui->paddingTopSpin->setValue(0);
        ui->paddingRightSpin->setValue(0);
        ui->paddingBottomSpin->setValue(0);
        ui->paddingLeftSpin->setValue(0);
        const QList<QCheckBox*> anchorChecks = {ui->anchorLeftCheckBox, ui->anchorRightCheckBox, ui->anchorTopCheckBox,
                                                ui->anchorBottomCheckBox, ui->anchorHCenterCheckBox, ui->anchorVCenterCheckBox};
        for(QCheckBox *check : anchorChecks)
        {
            if(check)
                check->setChecked(false);
        }

        const QList<QComboBox*> anchorCombos = {ui->anchorLeftTargetCombo, ui->anchorRightTargetCombo, ui->anchorTopTargetCombo,
                                                ui->anchorBottomTargetCombo, ui->anchorHCenterTargetCombo, ui->anchorVCenterTargetCombo};
        for(QComboBox *combo : anchorCombos)
        {
            if(combo)
                combo->setCurrentIndex(0);
        }

        const QList<QLineEdit*> anchorEdits = {ui->anchorLeftCustomLineEdit, ui->anchorRightCustomLineEdit, ui->anchorTopCustomLineEdit,
                                               ui->anchorBottomCustomLineEdit, ui->anchorHCenterCustomLineEdit, ui->anchorVCenterCustomLineEdit};
        for(QLineEdit *line : anchorEdits)
        {
            if(line)
            {
                line->clear();
                line->setEnabled(false);
            }
        }
        ui->anchorCenterInLineEdit->clear();
        ui->anchorFillLineEdit->clear();
        if(m_imageSourceLabel)
            m_imageSourceLabel->clear();
        if(ui->widgetTextRow)
            ui->widgetTextRow->setVisible(false);
        setPropertyEditorsEnabled(false);
        m_updatingProperties = false;
        return;
    }

    setPropertyEditorsEnabled(true);

    ui->widgetIdLineEdit->setText(widget->getId());
    const QPoint pos = widget->getPos();
    ui->posXSpin->setValue(pos.x());
    ui->posYSpin->setValue(pos.y());
    const QPoint size = widget->getSize();
    ui->widthSpin->setValue(qMax(1, size.x()));
    ui->heightSpin->setValue(qMax(1, size.y()));
    ui->opacitySpin->setValue(widget->opacity());
    ui->visibleCheckBox->setChecked(widget->isVisible());
    ui->phantomCheckBox->setChecked(widget->isPhantom());

    const QRect border = widget->getImageBorder();
    ui->borderLeftSpin->setValue(border.x());
    ui->borderTopSpin->setValue(border.y());
    ui->borderRightSpin->setValue(border.width());
    ui->borderBottomSpin->setValue(border.height());

    const auto &margin = widget->margin();
    ui->marginTopSpin->setValue(margin.top);
    ui->marginRightSpin->setValue(margin.right);
    ui->marginBottomSpin->setValue(margin.bottom);
    ui->marginLeftSpin->setValue(margin.left);

    const auto &padding = widget->padding();
    ui->paddingTopSpin->setValue(padding.top);
    ui->paddingRightSpin->setValue(padding.right);
    ui->paddingBottomSpin->setValue(padding.bottom);
    ui->paddingLeftSpin->setValue(padding.left);

    if(m_imageSourceLabel)
        m_imageSourceLabel->setText(widget->imageSource());

    ui->colorLineEdit->setText(widget->colorString());

    auto assignAnchorState = [&](QCheckBox *check,
                                 QComboBox *combo,
                                 QLineEdit *custom,
                                 OTUI::AnchorEdge edge) {
        if(!check || !combo || !custom)
            return;

        QSignalBlocker b1(check);
        QSignalBlocker b2(combo);
        QSignalBlocker b3(custom);

        const QString descriptor = widget->anchorDescriptor(edge);
        if(combo->count() == 0)
        {
            check->setChecked(false);
            custom->clear();
            custom->setEnabled(false);
            return;
        }
        if(descriptor.isEmpty())
        {
            check->setChecked(false);
            combo->setCurrentIndex(0);
            custom->clear();
        }
        else
        {
            check->setChecked(true);
            const QString targetId = descriptor.split('.', Qt::SkipEmptyParts).value(0).trimmed();
            bool matched = false;
            for(int i = 0; i < combo->count(); ++i)
            {
                const QString data = combo->itemData(i).toString();
                if(!data.isEmpty() && data.compare(targetId, Qt::CaseInsensitive) == 0)
                {
                    combo->setCurrentIndex(i);
                    matched = true;
                    break;
                }
            }
            if(!matched)
            {
                combo->setCurrentIndex(combo->count() - 1);
                custom->setText(targetId);
            }
            else
            {
                custom->clear();
            }
        }

        const bool enableCustom = check->isChecked() && combo->count() > 0 && combo->currentIndex() == combo->count() - 1;
        custom->setEnabled(enableCustom);
    };

    assignAnchorState(ui->anchorLeftCheckBox, ui->anchorLeftTargetCombo, ui->anchorLeftCustomLineEdit, OTUI::AnchorEdge::Left);
    assignAnchorState(ui->anchorRightCheckBox, ui->anchorRightTargetCombo, ui->anchorRightCustomLineEdit, OTUI::AnchorEdge::Right);
    assignAnchorState(ui->anchorTopCheckBox, ui->anchorTopTargetCombo, ui->anchorTopCustomLineEdit, OTUI::AnchorEdge::Top);
    assignAnchorState(ui->anchorBottomCheckBox, ui->anchorBottomTargetCombo, ui->anchorBottomCustomLineEdit, OTUI::AnchorEdge::Bottom);
    assignAnchorState(ui->anchorHCenterCheckBox, ui->anchorHCenterTargetCombo, ui->anchorHCenterCustomLineEdit, OTUI::AnchorEdge::HorizontalCenter);
    assignAnchorState(ui->anchorVCenterCheckBox, ui->anchorVCenterTargetCombo, ui->anchorVCenterCustomLineEdit, OTUI::AnchorEdge::VerticalCenter);

    ui->anchorCenterInLineEdit->setText(widget->centerInTarget());
    ui->anchorFillLineEdit->setText(widget->fillTarget());

    const bool hasText = widget->supportsTextProperty();
    if(ui->widgetTextRow)
        ui->widgetTextRow->setVisible(hasText);
    if(hasText)
        ui->widgetTextLineEdit->setText(widget->textProperty());
    else
        ui->widgetTextLineEdit->clear();

    m_updatingProperties = false;
}

void CoreWindow::handleImageSelection(const QString &sourcePath)
{
    if(imagesBrowser)
        imagesBrowser->hide();

    if(!m_selected || sourcePath.isEmpty())
        return;

    QString normalized = QDir::fromNativeSeparators(sourcePath);
    if(normalized.front() != '/')
        normalized.prepend('/');

    const QString dataPath = m_Project ? m_Project->getDataPath() : QString();
    m_selected->setImageSource(normalized, dataPath);
    if(m_imageSourceLabel)
        m_imageSourceLabel->setText(normalized);
    ui->openGLWidget->update();
    setProjectChanged(true);
}

void CoreWindow::handleStyleTemplateActivated(const QString &filePath, const QString &styleName)
{
    if(stylesBrowser)
        stylesBrowser->hide();
    instantiateStyleIntoSelection(filePath, styleName);
}

OTUI::Widget *CoreWindow::findWidgetById(const QString &widgetId) const
{
    if(!ui || widgetId.isEmpty())
        return nullptr;

    const auto &widgets = ui->openGLWidget->getOTUIWidgets();
    for(const auto &widget : widgets)
    {
        if(widget && widget->getId() == widgetId)
            return widget.get();
    }
    return nullptr;
}

bool CoreWindow::instantiateStyleIntoSelection(const QString &filePath, const QString &styleName)
{
    if(!m_Project)
    {
        ShowError("Style Error", "Open a project before instantiating styles.");
        return false;
    }

    OTUI::Widget *parentWidget = nullptr;
    if(ui->treeView && ui->treeView->currentIndex().isValid())
    {
        QStandardItem *item = model ? model->itemFromIndex(ui->treeView->currentIndex()) : nullptr;
        if(!item)
            return false;

        parentWidget = findWidgetById(item->text());
        if(!parentWidget)
        {
            ShowError("Selection Error", "Unable to locate the selected widget instance.");
            return false;
        }
    }
    else
    {
        if(!ui->openGLWidget)
            return false;
        if(!ui->openGLWidget->getOTUIWidgets().empty())
        {
            ShowError("Selection Required", "Select a parent widget in the tree before adding a style.");
            return false;
        }
    }

    OTUI::Parser::WidgetList widgets;
    QString error;
    if(!m_parser.instantiateStyle(filePath, styleName, widgets, &error, m_Project->getDataPath()))
    {
        ShowError("Style Error", error.isEmpty() ? QStringLiteral("Failed to instantiate style.") : error);
        return false;
    }

    if(widgets.empty())
        return false;

    OTUI::Widget *createdRoot = ui->openGLWidget->appendWidgetTree(parentWidget, std::move(widgets));
    if(!createdRoot)
    {
        ShowError("Style Error", "Unable to attach style to the selected widget.");
        return false;
    }

    m_selected = createdRoot;
    rebuildWidgetTree();
    syncTreeSelection(createdRoot);
    setProjectChanged(true);
    return true;
}

void CoreWindow::showStylesBrowser()
{
    if(!stylesBrowser)
        return;
    stylesBrowser->move(this->rect().center() - stylesBrowser->rect().center());
    stylesBrowser->show();
    stylesBrowser->raise();
}

void CoreWindow::applyAnchorsForWidget(OTUI::Widget *widget)
{
    if(!widget || !ui || !ui->openGLWidget)
        return;

    const auto &widgets = ui->openGLWidget->getOTUIWidgets();
    QHash<QString, OTUI::Widget*> lookup;
    lookup.reserve(static_cast<int>(widgets.size()));
    for(const auto &instance : widgets)
    {
        if(instance)
            lookup.insert(instance->getId(), instance.get());
    }

    auto findPreviousSibling = [&](const OTUI::Widget *target) -> OTUI::Widget* {
        if(!target)
            return nullptr;
        OTUI::Widget *parent = target->getParent();
        OTUI::Widget *previous = nullptr;
        for(const auto &instance : widgets)
        {
            OTUI::Widget *candidate = instance.get();
            if(candidate == target)
                break;
            if(candidate && candidate->getParent() == parent)
                previous = candidate;
        }
        return previous;
    };

    widget->applyAnchors([&](const QString &id) -> OTUI::Widget* {
        if(id.compare(QStringLiteral("prev"), Qt::CaseInsensitive) == 0 ||
           id.compare(QStringLiteral("previous"), Qt::CaseInsensitive) == 0)
        {
            return findPreviousSibling(widget);
        }
        return lookup.value(id, nullptr);
    });
}

void CoreWindow::syncTreeSelection(OTUI::Widget *widget)
{
    if(!widget || !model)
        return;

    if(!ui->treeView->selectionModel())
        return;

    const QModelIndexList matches = model->match(model->index(0, 0), Qt::DisplayRole, widget->getId(), 1, Qt::MatchRecursive);
    if(matches.isEmpty())
        return;

    const QModelIndex &index = matches.first();
    ui->treeView->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    ui->treeView->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}
