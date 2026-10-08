#pragma once

#include <QDialog>

class InstanceManager;
class QLabel;
class QListWidget;

// Screenshots browser: per-profile game/screenshots/ grid with a preview,
// open-folder, delete (to trash) and copy-path actions. Read-only toward the
// game: it never writes into saves or configs.
class ScreenshotsDialog : public QDialog {
    Q_OBJECT
public:
    ScreenshotsDialog(const QString &instanceId, InstanceManager *instances, const QString &dataDir,
                      QWidget *parent = nullptr);

private slots:
    void rebuild();
    void onPreview();
    void onOpenFolder();
    void onDelete();
    void onCopyPath();

private:
    static QString prettySize(qint64 bytes);

    QString m_instanceId;
    QString m_dataDir;
    InstanceManager *m_instances = nullptr;

    QListWidget *m_list = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_info = nullptr;
};
