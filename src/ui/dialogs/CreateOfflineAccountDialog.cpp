#include "dialogs/CreateOfflineAccountDialog.h"

#include "Account.h"
#include "AccountProvider.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QUuid>
#include <QVBoxLayout>

CreateOfflineAccountDialog::CreateOfflineAccountDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Create Offline Account"));
    setModal(true);
    setMinimumWidth(420);

    auto *lay = new QVBoxLayout(this);

    auto *intro = new QLabel(tr("Choose a name for this computer only. No sign-in, no internet needed."), this);
    intro->setWordWrap(true);
    lay->addWidget(intro);

    auto *form = new QFormLayout();
    m_name = new QLineEdit(this);
    m_name->setPlaceholderText(tr("e.g. River"));
    m_name->setMaxLength(16);
    m_name->setToolTip(usernameValidationHint());
    form->addRow(tr("Username"), m_name);

    m_color = new QComboBox(this);
    m_color->addItems(OfflineAccountProvider::presetAvatarColors());
    m_color->setToolTip(tr("Avatar tint used inside Hearthlight only."));
    form->addRow(tr("Avatar color"), m_color);
    lay->addLayout(form);

    m_customToggle = new QCheckBox(tr("Advanced: use a custom UUID"), this);
    lay->addWidget(m_customToggle);

    m_customUuid = new QLineEdit(this);
    m_customUuid->setPlaceholderText(tr("xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"));
    m_customUuid->setEnabled(false);
    lay->addWidget(m_customUuid);

    connect(m_customToggle, &QCheckBox::toggled, m_customUuid, &QLineEdit::setEnabled);

    m_error = new QLabel(this);
    m_error->setObjectName(QStringLiteral("secondary"));
    m_error->setWordWrap(true);
    lay->addWidget(m_error);

    auto *notice = new QLabel(
        tr("<b>Offline accounts are local profiles.</b> They do not provide Minecraft "
           "ownership and cannot authenticate to official online services."),
        this);
    notice->setWordWrap(true);
    notice->setObjectName(QStringLiteral("secondary"));
    lay->addWidget(notice);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Create"));
    lay->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_name, &QLineEdit::textChanged, this, &CreateOfflineAccountDialog::refreshValidity);
    connect(m_customUuid, &QLineEdit::textChanged, this, &CreateOfflineAccountDialog::refreshValidity);
    connect(m_customToggle, &QCheckBox::toggled, this, &CreateOfflineAccountDialog::refreshValidity);
    refreshValidity();
}

QString CreateOfflineAccountDialog::username() const
{
    return m_name->text().trimmed();
}

QString CreateOfflineAccountDialog::avatarColor() const
{
    return m_color->currentText();
}

bool CreateOfflineAccountDialog::useCustomUuid() const
{
    return m_customToggle->isChecked();
}

QString CreateOfflineAccountDialog::customUuidText() const
{
    return m_customUuid->text().trimmed();
}

void CreateOfflineAccountDialog::refreshValidity()
{
    QString err;
    const QString name = username();
    if (!isValidMinecraftUsername(name)) {
        err = usernameValidationHint();
    } else if (useCustomUuid()) {
        if (QUuid::fromString(customUuidText()).isNull()) {
            err = tr("That custom UUID doesn't look valid.");
        }
    }
    m_error->setText(err);
    auto *ok = findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok);
    if (ok) {
        ok->setEnabled(err.isEmpty());
    }
}
