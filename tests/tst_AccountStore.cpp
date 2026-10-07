// Tests: offline account serialization/deserialization, duplicates,
// type switching, Microsoft/offline separation, corrupted-data recovery.
#include "Account.h"
#include "AccountProvider.h"
#include "AccountStore.h"

#include <QFile>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class TstAccountStore : public QObject {
    Q_OBJECT
private slots:
    void roundTrip();
    void noTokensInJson();
    void rejectsTokenBearingJson();
    void duplicateUsernames();
    void duplicateUuids();
    void typeSwitching();
    void microsoftOfflineSeparation();
    void corruptedRecovery();
    void offlineProviderValidation();
};

void TstAccountStore::roundTrip()
{
    OfflineAccountProvider p;
    auto res = p.createAccount({ QStringLiteral("River"), QStringLiteral("#FFB347"), false, {} });
    QVERIFY(res.ok);
    const QJsonObject j = res.value.toJson();
    bool ok = false;
    const Account back = Account::fromJson(j, &ok);
    QVERIFY(ok);
    QCOMPARE(back.username, QStringLiteral("River"));
    QCOMPARE(back.uuid, res.value.uuid);
    QCOMPARE(back.type, AccountType::Offline);
    QCOMPARE(back.avatarColor, QStringLiteral("#FFB347"));
}

void TstAccountStore::noTokensInJson()
{
    OfflineAccountProvider p;
    const auto res = p.createAccount({ QStringLiteral("Steve"), {}, false, {} });
    QVERIFY(res.ok);
    const QString raw = QString::fromUtf8(QJsonDocument(res.value.toJson()).toJson());
    QVERIFY(!raw.contains(QStringLiteral("accessToken"), Qt::CaseInsensitive));
    QVERIFY(!raw.contains(QStringLiteral("refreshToken"), Qt::CaseInsensitive));
    QVERIFY(!raw.contains(QStringLiteral("xsts"), Qt::CaseInsensitive));
}

void TstAccountStore::rejectsTokenBearingJson()
{
    OfflineAccountProvider p;
    const auto res = p.createAccount({ QStringLiteral("Steve"), {}, false, {} });
    QVERIFY(res.ok);
    QJsonObject j = res.value.toJson();
    j[QStringLiteral("accessToken")] = QStringLiteral("fake");
    bool ok = true;
    Account::fromJson(j, &ok);
    QVERIFY(!ok); // smuggled credentials must invalidate the entry
}

void TstAccountStore::duplicateUsernames()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AccountStore store(dir.path());
    OfflineAccountProvider p;
    QString err;
    QVERIFY(store.addAccount(p.createAccount({ QStringLiteral("Steve"), {}, false, {} }).value, &err));
    // Same name case-insensitively must be rejected.
    QVERIFY(!store.addAccount(p.createAccount({ QStringLiteral("steve"), {}, false, {} }).value, &err));
    QVERIFY(!err.isEmpty());
    // A different name is fine.
    QVERIFY(store.addAccount(p.createAccount({ QStringLiteral("Alex"), {}, false, {} }).value, &err));
    QCOMPARE(store.accounts().size(), 2);
}

void TstAccountStore::duplicateUuids()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AccountStore store(dir.path());
    OfflineAccountProvider p;
    QString err;
    QVERIFY(store.addAccount(p.createAccount({ QStringLiteral("Steve"), {}, false, {} }).value, &err));
    // Custom UUID colliding with an existing account must be rejected.
    OfflineAccountProvider::CreateOptions opts;
    opts.username = QStringLiteral("SomeoneElse");
    opts.useCustomUuid = true;
    opts.customUuid = offlineUuidForUsername(QStringLiteral("Steve"));
    const auto res = p.createAccount(opts);
    QVERIFY(res.ok);
    QVERIFY(!store.addAccount(res.value, &err));
}

void TstAccountStore::typeSwitching()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AccountStore store(dir.path());
    OfflineAccountProvider p;
    QString err;
    QVERIFY(store.addAccount(p.createAccount({ QStringLiteral("Steve"), {}, false, {} }).value, &err));
    QVERIFY(store.addAccount(p.createAccount({ QStringLiteral("Alex"), {}, false, {} }).value, &err));
    QSignalSpy spy(&store, &AccountStore::activeAccountChanged);
    store.setActiveAccountId(store.accounts().at(1).id);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(store.activeAccount().username, QStringLiteral("Alex"));
    store.setActiveAccountId(store.accounts().at(0).id);
    QCOMPARE(store.activeAccount().username, QStringLiteral("Steve"));
}

void TstAccountStore::microsoftOfflineSeparation()
{
    // Microsoft and Offline entries share the Account shape but Microsoft
    // entries carry no tokens either — tokens live in SecureTokenStore.
    Account ms;
    ms.type = AccountType::Microsoft;
    ms.username = QStringLiteral("PlayerName");
    ms.uuid = QUuid::createUuid();
    ms.id = ms.uuid.toString(QUuid::WithoutBraces).toLower();
    const QJsonObject j = ms.toJson();
    QCOMPARE(j.value(QStringLiteral("type")).toString(), QStringLiteral("microsoft"));
    QVERIFY(!QJsonDocument(j).toJson().contains("Token"));
    QCOMPARE(ms.userTypeString(), QStringLiteral("msa"));

    Account off;
    off.type = AccountType::Offline;
    off.username = QStringLiteral("River");
    off.uuid = offlineUuidForUsername(off.username);
    off.id = off.uuid.toString(QUuid::WithoutBraces).toLower();
    QCOMPARE(off.userTypeString(), QStringLiteral("legacy"));

    // Type must never silently convert.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AccountStore store(dir.path());
    QString err;
    QVERIFY(store.addAccount(off, &err));
    Account mutated = off;
    mutated.type = AccountType::Microsoft;
    QVERIFY(!store.updateAccount(mutated, &err));
}

void TstAccountStore::corruptedRecovery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    {
        AccountStore store(dir.path());
        OfflineAccountProvider p;
        QString err;
        QVERIFY(store.addAccount(p.createAccount({ QStringLiteral("Steve"), {}, false, {} }).value, &err));
        QVERIFY(QFile::exists(store.accountsFilePath()));
        // Corrupt the file.
        QFile f(store.accountsFilePath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write("{ this is not json !!!");
        f.close();
    }
    AccountStore store2(dir.path());
    QSignalSpy loaded(&store2, &AccountStore::loaded);
    store2.load();
    QCOMPARE(loaded.size(), 1);
    QVERIFY(store2.accounts().isEmpty()); // recovered to empty, not crashed
    // Quarantine backup exists.
    const QDir d(dir.path() + QStringLiteral("/accounts"));
    QVERIFY(!d.entryList({ QStringLiteral("accounts.json.corrupt-*.bak") }, QDir::Files).isEmpty());
}

void TstAccountStore::offlineProviderValidation()
{
    OfflineAccountProvider p;
    QVERIFY(p.isAvailable());
    QVERIFY(!p.createAccount({ QStringLiteral("ab"), {}, false, {} }).ok);
    QVERIFY(!p.createAccount({ QStringLiteral("has space"), {}, false, {} }).ok);
    QVERIFY(p.createAccount({ QStringLiteral("Good_Name1"), {}, false, {} }).ok);
}

QTEST_MAIN(TstAccountStore)
#include "tst_AccountStore.moc"
