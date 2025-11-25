#include "modulescanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringList>

namespace {
QString trimValue(const QString& value)
{
    QString result = value.trimmed();
    if(result.startsWith('"') && result.endsWith('"') && result.size() >= 2)
        result = result.mid(1, result.size() - 2);
    else if(result.startsWith('\'') && result.endsWith('\'') && result.size() >= 2)
        result = result.mid(1, result.size() - 2);
    return result.trimmed();
}
}

bool ModuleScanner::scan(const QString& otmodPath,
                         const QString& dataPathHint,
                         Result& outResult,
                         QString* error) const
{
    QFileInfo modInfo(otmodPath);
    if(!modInfo.exists() || !modInfo.isFile()) {
        if(error)
            *error = QStringLiteral("Module file not found: %1").arg(otmodPath);
        return false;
    }

    QFile file(otmodPath);
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if(error)
            *error = QStringLiteral("Unable to open %1").arg(otmodPath);
        return false;
    }

    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    Result result;
    result.moduleDir = modInfo.absolutePath();

    QRegularExpression nameRegex(QStringLiteral("name\\s*:\\s*([^\\r\\n]+)"));
    QRegularExpressionMatch nameMatch = nameRegex.match(content);
    if(nameMatch.hasMatch())
        result.moduleName = nameMatch.captured(1).trimmed();
    if(result.moduleName.isEmpty())
        result.moduleName = modInfo.completeBaseName();

    QRegularExpression scriptsRegex(QStringLiteral("scripts\\s*:\\s*\\[(.*?)\\]"), QRegularExpression::DotMatchesEverythingOption);
    QRegularExpressionMatch scriptsMatch = scriptsRegex.match(content);
    QStringList scriptNames;
    if(scriptsMatch.hasMatch()) {
        const QString rawList = scriptsMatch.captured(1);
        for(const QString& entry : rawList.split(',', Qt::SkipEmptyParts))
            scriptNames << entry.trimmed();
    }

    if(scriptNames.isEmpty())
        scriptNames << modInfo.completeBaseName();

    QSet<QString> processedScripts;
    QSet<QString> collectedAbsolutePaths;

    for(const QString& scriptName : std::as_const(scriptNames)) {
        const QString scriptPath = resolveScriptPath(scriptName, result.moduleDir);
        if(scriptPath.isEmpty())
            continue;
        processScript(scriptPath,
                  result.moduleDir,
                  dataPathHint,
                  result,
                  processedScripts,
                  collectedAbsolutePaths);
    }

    if(result.entries.isEmpty()) {
        if(error) {
            *error = QStringLiteral("No OTUI files were found in module %1.").arg(result.moduleName);
            if(!result.missingUiFiles.isEmpty())
                *error += QStringLiteral("\nMissing UI files: %1").arg(result.missingUiFiles.join(", "));
        }
        return false;
    }

    if(result.primaryIndex > 0 && result.primaryIndex < result.entries.size()) {
        auto primary = result.entries.takeAt(result.primaryIndex);
        result.entries.prepend(primary);
        result.primaryIndex = 0;
    } else if(result.primaryIndex == -1) {
        result.primaryIndex = 0;
    }

    outResult = result;
    return true;
}

void ModuleScanner::processScript(const QString& scriptPath,
                                  const QString& moduleDir,
                                  const QString& dataPathHint,
                                  Result& outResult,
                                  QSet<QString>& processedScripts,
                                  QSet<QString>& collectedAbsolutePaths) const
{
    QFileInfo info(scriptPath);
    if(!info.exists() || !info.isFile())
        return;

    const QString canonical = info.canonicalFilePath().isEmpty() ? info.absoluteFilePath() : info.canonicalFilePath();
    if(processedScripts.contains(canonical))
        return;
    processedScripts.insert(canonical);

    QFile file(info.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const QString content = QString::fromUtf8(file.readAll());
    file.close();

    collectUiReferences(content,
                        moduleDir,
                        dataPathHint,
                        outResult,
                        collectedAbsolutePaths);

    QSet<QString> nestedScripts;
    collectNestedScripts(content, moduleDir, nestedScripts);
    for(const QString& nested : std::as_const(nestedScripts))
        processScript(nested, moduleDir, dataPathHint, outResult, processedScripts, collectedAbsolutePaths);
}

void ModuleScanner::collectUiReferences(const QString& scriptContent,
                                        const QString& moduleDir,
                                        const QString& dataPathHint,
                                        Result& outResult,
                                        QSet<QString>& collectedAbsolutePaths) const
{
    auto appendEntry = [&](const QString& rawPath, bool isPrimaryCandidate) {
        const QString absolutePath = resolveUiPath(rawPath, moduleDir, dataPathHint);
        if(absolutePath.isEmpty())
            return;

        QFileInfo uiInfo(absolutePath);
        if(!uiInfo.exists()) {
            if(!outResult.missingUiFiles.contains(absolutePath))
                outResult.missingUiFiles << absolutePath;
            return;
        }

        const QString canonical = uiInfo.canonicalFilePath().isEmpty() ? uiInfo.absoluteFilePath() : uiInfo.canonicalFilePath();
        if(collectedAbsolutePaths.contains(canonical))
            return;

        collectedAbsolutePaths.insert(canonical);
        Entry entry;
        entry.absolutePath = uiInfo.absoluteFilePath();
        QString relative = QDir(moduleDir).relativeFilePath(entry.absolutePath);
        if(relative.startsWith(".."))
            relative = entry.absolutePath;
        entry.label = relative;
        outResult.entries.append(entry);
        if(isPrimaryCandidate && outResult.primaryIndex < 0) {
            outResult.primaryIndex = outResult.entries.size() - 1;
        }
    };

    QRegularExpression setUiRegex(QStringLiteral("controller\\s*:\\s*setUI\\s*\\(\\s*['\"]([^'\"]+)['\"]"));
    auto setUiIt = setUiRegex.globalMatch(scriptContent);
    while(setUiIt.hasNext()) {
        const QString path = setUiIt.next().captured(1);
        appendEntry(path, true);
    }

    QRegularExpression loadUiRegex(QStringLiteral("g_ui\\.(?:loadUI|displayUI|importStyle)\\s*\\(\\s*['\"]([^'\"]+)['\"]"));
    auto loadIt = loadUiRegex.globalMatch(scriptContent);
    while(loadIt.hasNext()) {
        const QString path = loadIt.next().captured(1);
        appendEntry(path, false);
    }
}

void ModuleScanner::collectNestedScripts(const QString& scriptContent,
                                         const QString& moduleDir,
                                         QSet<QString>& pendingScripts) const
{
    QRegularExpression dofileRegex(QStringLiteral("dofile\\s*\\(\\s*['\"]([^'\"]+)['\"]"));
    auto it = dofileRegex.globalMatch(scriptContent);
    while(it.hasNext()) {
        const QString raw = it.next().captured(1);
        const QString scriptPath = resolveScriptPath(raw, moduleDir);
        if(!scriptPath.isEmpty())
            pendingScripts.insert(scriptPath);
    }
}

QString ModuleScanner::resolveUiPath(const QString& rawPath,
                                     const QString& moduleDir,
                                     const QString& dataPathHint) const
{
    QString path = trimValue(rawPath);
    if(path.isEmpty())
        return QString();

    if(!path.endsWith(QStringLiteral(".otui"), Qt::CaseInsensitive))
        path += QStringLiteral(".otui");

    path.replace('\\', '/');
    QString absolute;

    if(QDir::isAbsolutePath(path)) {
        absolute = QDir::cleanPath(path);
    } else if(path.startsWith('/')) {
        if(dataPathHint.isEmpty())
            return QString();
        QString relative = path.mid(1);
        absolute = QDir(dataPathHint).filePath(relative);
    } else {
        absolute = QDir(moduleDir).filePath(path);
    }

    return QDir::cleanPath(absolute);
}

QString ModuleScanner::resolveScriptPath(const QString& rawPath,
                                         const QString& moduleDir) const
{
    QString path = trimValue(rawPath);
    if(path.isEmpty())
        return QString();

    if(!path.endsWith(QStringLiteral(".lua"), Qt::CaseInsensitive))
        path += QStringLiteral(".lua");

    path.replace('\\', '/');
    QString absolute;

    if(QDir::isAbsolutePath(path))
        absolute = QDir::cleanPath(path);
    else if(path.startsWith('/'))
        absolute = QDir(moduleDir).filePath(path.mid(1));
    else
        absolute = QDir(moduleDir).filePath(path);

    return QDir::cleanPath(absolute);
}
