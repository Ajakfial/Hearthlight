#pragma once

#include <QWidget>

class AccountStore;
class JavaManager;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class SecureTokenStore;
class Theme;
class QSpinBox;
class QSlider;
class AppSettings;

// Settings page: General (offline mode, data folder, launch behavior),
// Appearance (accent, scale), Java (detected JVMs, custom path + test),
// Downloads (parallelism, cache size, clear), Mod sources (CurseForge API
// key in the OS credential store), Privacy, About (version, disclaimer).
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(AppSettings *settings, AccountStore *store, Theme *theme, QWidget *parent = nullptr);

    void setJavaManager(JavaManager *jm);
    void setTokenStore(SecureTokenStore *tokens);

private slots:
    void applyOfflineMode(int idx);
    void pickAccent();
    void testJava();
    void clearCache();
    void refreshJavaList();
    void applyCloseBehavior(int idx);
    void saveCurseForgeKey();
    void clearCurseForgeKey();
    void refreshCurseForgeKeyState();

private:
    AppSettings *m_settings = nullptr;
    AccountStore *m_store = nullptr;
    Theme *m_theme = nullptr;
    JavaManager *m_java = nullptr;
    QLabel *m_cacheLabel = nullptr;
    QComboBox *m_offlineBox = nullptr;
    QComboBox *m_javaList = nullptr;
    QComboBox *m_closeBox = nullptr;
    QLabel *m_javaStatus = nullptr;
    SecureTokenStore *m_tokens = nullptr;
    QLineEdit *m_cfKeyEdit = nullptr;
    QLabel *m_cfKeyState = nullptr;
};
