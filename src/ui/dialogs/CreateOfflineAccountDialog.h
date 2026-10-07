#pragma once

#include <QDialog>
#include <QLineEdit>

class QCheckBox;
class QComboBox;
class QLabel;

// "Create Offline Account" dialog (spec 6.2):
//  Username + optional avatar color + optional custom UUID (advanced).
//  Shows the required local-profile notice. No network contact.
class CreateOfflineAccountDialog : public QDialog {
    Q_OBJECT
public:
    explicit CreateOfflineAccountDialog(QWidget *parent = nullptr);

    QString username() const;
    QString avatarColor() const;
    bool useCustomUuid() const;
    QString customUuidText() const;

private:
    void refreshValidity();

    QLineEdit *m_name = nullptr;
    QComboBox *m_color = nullptr;
    QCheckBox *m_customToggle = nullptr;
    QLineEdit *m_customUuid = nullptr;
    QLabel *m_error = nullptr;
};
