#pragma once

#include "Instance.h"

#include <QDialog>

class AccountStore;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QPlainTextEdit;

// Per-instance settings (spec 7): Java, memory, JVM args, window size,
// fullscreen, pre-launch/post-exit commands, environment variables,
// game-directory override, preferred account, Quick Play target, group.
class InstanceSettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit InstanceSettingsDialog(const Instance &in, AccountStore *accounts, QWidget *parent = nullptr);
    Instance result() const { return m_result; }

private slots:
    void onPickJava();
    void onTestJava();
    void onAccountModeChanged(int idx);

private:
    Instance m_result;
    AccountStore *m_accounts = nullptr;
    QComboBox *m_accountMode = nullptr;
    QComboBox *m_accountId = nullptr;
    QLineEdit *m_java = nullptr;
    QSpinBox *m_memory = nullptr;
    QLineEdit *m_jvm = nullptr;
    QSpinBox *m_width = nullptr;
    QSpinBox *m_height = nullptr;
    QCheckBox *m_full = nullptr;
    QLineEdit *m_pre = nullptr;
    QLineEdit *m_post = nullptr;
    QPlainTextEdit *m_env = nullptr;
    QLineEdit *m_gameDir = nullptr;
    QLineEdit *m_group = nullptr;
    QLineEdit *m_qpWorld = nullptr;
    QLineEdit *m_qpServer = nullptr;
    QLineEdit *m_qpRealm = nullptr;
};
