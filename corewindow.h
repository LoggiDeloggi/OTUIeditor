#ifndef COREWINDOW_H
#define COREWINDOW_H

#include <QMainWindow>
#include <QMessageBox>
#include <QStandardItem>
#include <QItemSelectionModel>
#include <QKeyEvent>

#include "otui/otui.h"
#include "otui/parser.h"

#include "events/setidevent.h"
#include "events/settingssavedevent.h"

#include "imagesourcebrowser.h"
#include "stylesourcebrowser.h"
#include "elidedlabel.h"
#include "projectsettings.h"

class QPushButton;

namespace Ui {
class MainWindow;
}

class CoreWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CoreWindow(QWidget *parent = nullptr);
    ~CoreWindow();
    static void ShowError(QString title, QString description);

    void startNewProject(QString fileName, QString name, QString path, QString dataPath);
    void loadProjectData(QDataStream &data, QString fileName, QString path);

private slots:
    void on_newMainWindow_triggered();

    void on_newButton_triggered();

    void on_newLabel_triggered();

    void on_actionDeleteWidget_triggered();

    void on_treeView_customContextMenuRequested(const QPoint &pos);

    void on_actionNewProject_triggered();

    void on_actionOpenProject_triggered();

    void on_actionOpenModule_triggered();

    void on_actionSaveProject_triggered();

    void on_actionCloseProject_triggered();

    void on_horizontalSlider_valueChanged(int value);

    void on_newUIItem_triggered();

    void on_newUICreature_triggered();

    void on_actionProject_Settings_triggered();

    void on_newImage_triggered();

    void handleStyleTemplateActivated(const QString &filePath, const QString &styleName);

protected:
    bool eventFilter(QObject *obj, QEvent *ev);
    void keyReleaseEvent(QKeyEvent *event);
    bool event(QEvent *event);
    void closeEvent(QCloseEvent *event);
    void resizeEvent(QResizeEvent *event);

private:
    void initializeWindow();
    void initializePropertyPanel();
    void connectPropertyEditors();
    void setPropertyEditorsEnabled(bool enabled);
    void updatePropertyPanel(OTUI::Widget *widget);
    void setProjectChanged(bool v);
    bool importOtuiFile(const QString &filePath, const QString &dataPathOverride = QString());
    void rebuildWidgetTree();
    void handleImageSelection(const QString &sourcePath);
    void syncTreeSelection(OTUI::Widget *widget);
    OTUI::Widget *findWidgetById(const QString &widgetId) const;
    bool instantiateStyleIntoSelection(const QString &filePath, const QString &styleName);
    void showStylesBrowser();
    void applyAnchorsForWidget(OTUI::Widget *widget);

private:
    Ui::MainWindow *ui;

    OTUI::Project *m_Project = nullptr;
    OTUI::Parser m_parser;

    QStandardItemModel *model = nullptr;

    void addChildToTree(QString label);
    void selectWidgetById(QString widgetId);

    OTUI::Widget *m_selected = nullptr;
    ImageSourceBrowser *imagesBrowser = nullptr;
    StyleSourceBrowser *stylesBrowser = nullptr;
    ProjectSettings *m_projectSettings = nullptr;

    QString m_currentOtuiPath;
    bool m_updatingProperties = false;
    ElidedLabel *m_imageSourceLabel = nullptr;
    QPushButton *m_imageBrowseButton = nullptr;
};

#endif // COREWINDOW_H
