#ifndef STYLESOURCEBROWSER_H
#define STYLESOURCEBROWSER_H

#include <QFrame>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QStringList>
#include <QVector>

#include "otui/parser.h"

class StyleSourceBrowser : public QFrame
{
    Q_OBJECT
public:
    explicit StyleSourceBrowser(QWidget *parent = nullptr);

    void setDataPath(const QString &path);
    void initialize();
    void refresh();

    struct StyleTemplateEntry {
        QString filePath;
        QString styleName;
        QString displayName;
    };
    const QVector<StyleTemplateEntry> &styleTemplates() const { return m_styleEntries; }

signals:
    void styleActivated(const QString &filePath);
    void styleTemplateActivated(const QString &filePath, const QString &styleName);

private slots:
    void handleItemDoubleClicked(QTreeWidgetItem *item, int column);

private:
    void rebuildTree();
    void addRootListing(const QString &title, const QString &rootPath);
    QStringList collectOtuiFiles(const QString &rootPath) const;
    QStringList collectStyleEntries(const QString &filePath) const;
    void addFileEntry(QTreeWidgetItem *parentItem, const QString &rootPath, const QString &filePath);

    enum class EntryType {
        File,
        Style
    };

private:
    QString m_dataPath;
    QTreeWidget *m_tree;
    OTUI::Parser m_parser;
    QVector<StyleTemplateEntry> m_styleEntries;
};

#endif // STYLESOURCEBROWSER_H
