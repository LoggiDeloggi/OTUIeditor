#ifndef MODULESCANNER_H
#define MODULESCANNER_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QSet>

class ModuleScanner
{
public:
    struct Entry {
        QString label;
        QString absolutePath;
    };

    struct Result {
        QString moduleName;
        QString moduleDir;
        QVector<Entry> entries;
        int primaryIndex = -1;
        QStringList missingUiFiles;
    };

    bool scan(const QString& otmodPath,
              const QString& dataPathHint,
              Result& outResult,
              QString* error = nullptr) const;

private:
    void processScript(const QString& scriptPath,
                       const QString& moduleDir,
                       const QString& dataPathHint,
                       Result& outResult,
                       QSet<QString>& processedScripts,
                       QSet<QString>& collectedAbsolutePaths) const;

    void collectUiReferences(const QString& scriptContent,
                             const QString& moduleDir,
                             const QString& dataPathHint,
                             Result& outResult,
                             QSet<QString>& collectedAbsolutePaths) const;

    void collectNestedScripts(const QString& scriptContent,
                              const QString& moduleDir,
                              QSet<QString>& pendingScripts) const;

    QString resolveUiPath(const QString& rawPath,
                          const QString& moduleDir,
                          const QString& dataPathHint) const;

    QString resolveScriptPath(const QString& rawPath,
                              const QString& moduleDir) const;
};

#endif // MODULESCANNER_H
