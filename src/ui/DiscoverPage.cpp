#include "DiscoverPage.h"

#include "Embers.h"
#include "IconProvider.h"
#include "InstanceManager.h"
#include "Logger.h"
#include "Markdown.h"
#include "ModManager.h"
#include "ModrinthApi.h"
#include "MojangApi.h"
#include "NetworkStatus.h"
#include "Task.h"
#include "dialogs/TaskProgressDialog.h"

#include <QCryptographicHash>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

#include <memory>

#include "DownloadManager.h"

// ---------------------------------------------------------------------------
// ProjectDialog
// ---------------------------------------------------------------------------

ProjectDialog::ProjectDialog(const ModrinthSearchHit &hit, MojangApi *mojang, InstanceManager *instances,
                             ModrinthApi *modrinth, ModManager *mods, Embers *embers, const QString &dataDir,
                             QWidget *parent)
    : QDialog(parent)
    , m_hit(hit)
    , m_mojang(mojang)
    , m_instances(instances)
    , m_modrinth(modrinth)
    , m_mods(mods)
    , m_embers(embers)
    , m_dataDir(dataDir)
{
    setWindowTitle(hit.title.isEmpty() ? tr("Mod details") : hit.title);
    resize(860, 620);
    m_nam = new QNetworkAccessManager(this);

    auto *lay = new QVBoxLayout(this);
    auto *head = new QHBoxLayout();
    m_icon = new QLabel(this);
    m_icon->setFixedSize(64, 64);
    m_icon->setScaledContents(true);
    head->addWidget(m_icon);
    m_title = new QLabel(tr("Loading…"), this);
    QFont tf = m_title->font();
    tf.setPointSize(16);
    tf.setBold(true);
    m_title->setFont(tf);
    m_title->setWordWrap(true);
    head->addWidget(m_title, 1);
    lay->addLayout(head);
    if (!hit.iconUrl.isEmpty()) {
        requestImage(hit.iconUrl, m_icon, 64);
    }

    auto *tabs = new QTabWidget(this);
    m_about = new QTextBrowser(tabs);
    m_about->setOpenExternalLinks(true);
    tabs->addTab(m_about, tr("About"));
    auto *verTab = new QWidget(tabs);
    auto *verLay = new QVBoxLayout(verTab);
    verLay->setContentsMargins(0, 0, 0, 0);
    m_versionList = new QListWidget(verTab);
    verLay->addWidget(m_versionList, 1);
    m_changelog = new QTextBrowser(verTab);
    m_changelog->setOpenExternalLinks(true);
    m_changelog->setMaximumHeight(160);
    verLay->addWidget(m_changelog);
    tabs->addTab(verTab, tr("Versions"));
    auto *galTab = new QWidget(tabs);
    auto *galLay = new QVBoxLayout(galTab);
    galLay->setContentsMargins(8, 8, 8, 8);
    m_galleryRow = new QWidget(galTab);
    auto *grow = new QHBoxLayout(m_galleryRow);
    grow->setContentsMargins(0, 0, 0, 0);
    grow->addStretch(1);
    galLay->addWidget(m_galleryRow);
    galLay->addStretch(1);
    tabs->addTab(galTab, tr("Gallery"));
    lay->addWidget(tabs, 1);
    connect(m_versionList, &QListWidget::currentRowChanged, this, &ProjectDialog::onVersionSelected);

    auto *bot = new QHBoxLayout();
    bot->addWidget(new QLabel(tr("Install to:"), this));
    m_installTo = new QComboBox(this);
    bot->addWidget(m_installTo, 1);
    m_installBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("download")), tr("Install"), this);
    connect(m_installBtn, &QPushButton::clicked, this, &ProjectDialog::onInstall);
    bot->addWidget(m_installBtn);
    lay->addLayout(bot);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("secondary"));
    m_status->setWordWrap(true);
    lay->addWidget(m_status);

    // Target instances.
    for (const auto &in : m_instances->instances()) {
        const QString loader = in.loaderType == QStringLiteral("vanilla")
            ? tr("Vanilla")
            : QStringLiteral("%1 %2").arg(in.loaderType, in.loaderVersion);
        m_installTo->addItem(QStringLiteral("%1 (Minecraft %2, %3)").arg(in.name, in.versionId, loader), in.id);
    }
    if (m_installTo->count() == 0) {
        m_installTo->addItem(tr("(no profiles yet — create one on the Profiles page)"), QString());
        m_installBtn->setEnabled(false);
    }

    // Load full project in the background.
    m_status->setText(tr("Loading project details…"));
    auto *t = new LambdaTask(
        tr("Load %1").arg(hit.title),
        [this, hit](Task::Context &ctx) {
            m_project = m_modrinth->projectBlocking(hit.projectId.isEmpty() ? hit.slug : hit.projectId, ctx);
            return !m_project.id.isEmpty();
        },
        this);
    connect(t, &Task::finished, this, [this, t](bool ok) {
        t->deleteLater();
        if (!ok) {
            m_status->setText(t->errorString());
            m_title->setText(m_hit.title);
            m_about->setHtml(QStringLiteral("<p>") + tr("Couldn't load the details.") + QStringLiteral("</p>"));
            return;
        }
        m_loaded = true;
        renderHeader(m_project);
        loadVersions();
    });
    t->start();
}

void ProjectDialog::requestImage(const QString &url, QLabel *label, int size)
{
    Q_UNUSED(size);
    QNetworkRequest req{ QUrl(url) };
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("hearthlight/0.1.0"));
    QNetworkReply *rep = m_nam->get(req);
    rep->setParent(label); // labels outlive their fetch: closing the dialog kills pending ones
    connect(rep, &QNetworkReply::finished, this, [rep, label] {
        rep->deleteLater();
        if (rep->error() != QNetworkReply::NoError) {
            return;
        }
        QPixmap pm;
        if (pm.loadFromData(rep->readAll()) && !pm.isNull()) {
            label->setPixmap(pm);
        }
    });
}

void ProjectDialog::renderHeader(const ModrinthProject &p)
{
    m_title->setText(p.title);
    setWindowTitle(p.title);
    QString html = QStringLiteral("<p><b>%1</b> %2<br/>").arg(tr("by"), p.author.isEmpty() ? tr("unknown") : p.author);
    html += ModrinthMeta::prettyCount(p.downloads) + tr(" downloads") + QStringLiteral("</p>");
    if (!p.summary.isEmpty()) {
        html += QStringLiteral("<p><i>") + p.summary.toHtmlEscaped() + QStringLiteral("</i></p>");
    }
    html += Markdown::toHtml(p.body.isEmpty() ? tr("(No description provided.)") : p.body);
    html += QStringLiteral("<hr/><p>");
    if (!p.licenseName.isEmpty() || !p.licenseId.isEmpty()) {
        html += tr("License: ") + (p.licenseName.isEmpty() ? p.licenseId : p.licenseName).toHtmlEscaped()
            + QStringLiteral("<br/>");
    }
    auto link = [](const QString &label, const QString &url) {
        return url.isEmpty() ? QString()
                             : QStringLiteral("<a href=\"%1\">%2</a> ").arg(url.toHtmlEscaped(), label);
    };
    html += link(tr("Source"), p.sourceUrl) + link(tr("Issues"), p.issuesUrl) + link(tr("Wiki"), p.wikiUrl)
        + link(tr("Discord"), p.discordUrl) + QStringLiteral("</p>");
    m_about->setHtml(html);
    if (!p.iconUrl.isEmpty()) {
        requestImage(p.iconUrl, m_icon, 64);
    }
    // Gallery thumbs.
    auto *grow = qobject_cast<QHBoxLayout *>(m_galleryRow->layout());
    int shown = 0;
    for (const auto &g : p.gallery) {
        if (shown >= 6) {
            break;
        }
        auto *thumb = new QLabel(m_galleryRow);
        thumb->setFixedSize(128, 96);
        thumb->setScaledContents(true);
        thumb->setToolTip(g.title.isEmpty() ? g.url : g.title);
        thumb->setCursor(Qt::PointingHandCursor);
        thumb->setProperty("fullUrl", g.url);
        thumb->installEventFilter(this);
        grow->insertWidget(grow->count() - 1, thumb);
        requestImage(g.url, thumb, 128);
        ++shown;
    }
    if (shown == 0) {
        grow->insertWidget(0, new QLabel(tr("No gallery images."), m_galleryRow));
    }
}

bool ProjectDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        if (auto *l = qobject_cast<QLabel *>(watched)) {
            const QString url = l->property("fullUrl").toString();
            if (!url.isEmpty()) {
                QDesktopServices::openUrl(QUrl(url));
                return true;
            }
        }
    }
    return QDialog::eventFilter(watched, event);
}

void ProjectDialog::loadVersions()
{
    m_versionList->clear();
    m_status->setText(tr("Loading versions…"));
    const QString instId = m_installTo->currentData().toString();
    const Instance inst = m_instances->get(instId);
    const QString loader = inst.isValid() ? inst.loaderType : QString();
    const QString mc = inst.isValid() ? inst.versionId : QString();
    auto *t = new LambdaTask(
        tr("Load versions"),
        [this, loader, mc](Task::Context &ctx) {
            m_versions = m_modrinth->versionsBlocking(m_project.id, loader == QStringLiteral("vanilla") ? QString() : loader, mc, ctx);
            return true; // empty list is valid (filtered out); errors fail ctx
        },
        this);
    connect(t, &Task::finished, this, [this, t, mc, loader](bool ok) {
        const QString err = t->errorString();
        t->deleteLater();
        m_versionList->blockSignals(true);
        m_versionList->clear();
        if (!ok) {
            m_status->setText(err);
        } else {
            if (m_versions.isEmpty()) {
                m_status->setText(tr("No versions found for this filter."));
            } else {
                m_status->setText(tr("%1 version(s). Green = works with the selected profile.").arg(m_versions.size()));
            }
            for (const auto &v : m_versions) {
                const bool compat = ModrinthMeta::versionCompatible(v, mc, loader, m_project.projectType);
                auto *it = new QListWidgetItem(
                    QStringLiteral("%1%2  —  %3").arg(compat ? QStringLiteral("● ") : QStringLiteral("○ "),
                                                     v.versionNumber, v.name),
                    m_versionList);
                it->setData(Qt::UserRole, v.id);
                if (!compat) {
                    it->setForeground(QColor(QStringLiteral("#8E8E93")));
                }
            }
            if (m_versionList->count() > 0) {
                m_versionList->setCurrentRow(0);
            }
        }
        m_versionList->blockSignals(false);
        onVersionSelected();
    });
    t->start();
}

void ProjectDialog::onVersionSelected()
{
    const QString vid = m_versionList->currentItem() ? m_versionList->currentItem()->data(Qt::UserRole).toString() : QString();
    for (const auto &v : m_versions) {
        if (v.id == vid) {
            QString deps;
            for (const auto &d : v.dependencies) {
                if (!deps.isEmpty()) {
                    deps += QStringLiteral(", ");
                }
                deps += d.type + (d.projectId.isEmpty() ? QString() : QStringLiteral(":%1").arg(d.projectId.left(8)));
            }
            m_changelog->setHtml(Markdown::toHtml(v.changelog.isEmpty() ? tr("(No changelog.)") : v.changelog)
                                                 + (deps.isEmpty() ? QString() : QStringLiteral("\n\n---\nDependencies: ") + deps));
            return;
        }
    }
    m_changelog->clear();
}

ModrinthVersion ProjectDialog::selectedOrBest(QString *error)
{
    const QString instId = m_installTo->currentData().toString();
    const Instance inst = m_instances->get(instId);
    if (!inst.isValid()) {
        if (error) {
            *error = tr("Pick a profile to install into first.");
        }
        return {};
    }
    const QString vid = m_versionList->currentItem() ? m_versionList->currentItem()->data(Qt::UserRole).toString() : QString();
    for (const auto &v : m_versions) {
        if (v.id == vid) {
            if (!ModrinthMeta::versionCompatible(v, inst.versionId, inst.loaderType, m_project.projectType)) {
                if (error) {
                    *error = tr("That version doesn't work with “%1” (Minecraft %2, %3). Pick a green one.")
                                 .arg(inst.name, inst.versionId, inst.loaderType);
                }
                return {};
            }
            return v;
        }
    }
    const int best = ModrinthMeta::pickBestVersion(m_versions, inst.versionId, inst.loaderType, m_project.projectType);
    if (best < 0) {
        if (error) {
            *error = tr("Nothing here works with “%1” (Minecraft %2, %3).").arg(inst.name, inst.versionId, inst.loaderType);
        }
        return {};
    }
    return m_versions.at(best);
}

void ProjectDialog::onInstall()
{
    const QString instId = m_installTo->currentData().toString();
    Instance inst = m_instances->get(instId);
    if (!inst.isValid()) {
        QMessageBox::information(this, tr("Install"), tr("Create a profile on the Profiles page first."));
        return;
    }
    QString pickErr;
    const ModrinthVersion ver = selectedOrBest(&pickErr);
    if (ver.id.isEmpty()) {
        QMessageBox::information(this, tr("Install"), pickErr);
        return;
    }
    // Vanilla + mod = honest stop (resource packs/shaders/datapacks are fine).
    if (inst.loaderType == QStringLiteral("vanilla")
        && (m_project.projectType == QStringLiteral("mod") || m_project.projectType == QStringLiteral("modpack"))) {
        QMessageBox::information(
            this, tr("Needs a mod loader"),
            tr("“%1” is a %2, and “%3” is a vanilla profile. Switch the profile to Fabric, Quilt, Forge or "
               "NeoForge first (Profiles → right-click → Switch loader/version).")
                .arg(m_project.title, m_project.projectType, inst.name));
        return;
    }
    // Modpacks become brand-new profiles.
    if (m_project.projectType == QStringLiteral("modpack")) {
        const ModrinthFile f = ver.bestFile();
        if (f.url.isEmpty() || !f.filename.endsWith(QStringLiteral(".mrpack"))) {
            QMessageBox::information(this, tr("Install"),
                                     tr("That pack version has no downloadable .mrpack file."));
            return;
        }
        // Download the .mrpack to cache, then hand to the normal importer.
        auto *t = new LambdaTask(
            tr("Fetch %1").arg(m_project.title),
            [this, f](Task::Context &ctx) {
                const QString dest = QDir(m_dataDir)
                                         .filePath(QStringLiteral("cache/modrinth-packs/%1").arg(f.filename));
                QDir().mkpath(QFileInfo(dest).absolutePath());
                DownloadRequest req{ QUrl(f.url), dest };
                req.expectedSha512 = f.sha512.toLatin1();
                req.expectedSize = f.size;
                req.resume = false;
                QString err;
                if (!DownloadManager::downloadManyBlocking({ req }, 1, ctx, &err)) {
                    ctx.fail(err);
                    return false;
                }
                return true;
            },
            this);
        TaskProgressDialog prog(t, this);
        connect(t, &Task::finished, this, [this, t, f](bool ok) {
            t->deleteLater();
            if (!ok) {
                QMessageBox::warning(this, tr("Couldn't fetch that pack"), t->errorString());
                return;
            }
            const QString dest =
                QDir(m_dataDir).filePath(QStringLiteral("cache/modrinth-packs/%1").arg(f.filename));
            emit installModpackFile(dest);
            accept();
        });
        t->start();
        prog.exec();
        return;
    }
    // Resolve + confirm.
    ModInstallPlan root;
    root.project = m_project;
    root.version = ver;
    root.file = ver.bestFile();
    root.targetDir = ModrinthMeta::targetDirForType(m_project.projectType);
    auto *t = new LambdaTask(
        tr("Resolve %1").arg(m_project.title),
        [this, root, inst](Task::Context &ctx) mutable {
            QStringList warnings;
            QString err;
            const auto plan = m_modrinth->resolveInstallPlan({ root }, inst.versionId, inst.loaderType, ctx,
                                                             &warnings, &err);
            if (plan.isEmpty() && !err.isEmpty()) {
                ctx.fail(err);
                return false;
            }
            if (plan.isEmpty()) {
                ctx.fail(tr("Nothing to install."));
                return false;
            }
            QStringList lines;
            for (const auto &u : plan) {
                lines.append(QStringLiteral("%1 %2%3").arg(
                    u.project.title, u.version.versionNumber, u.isDependency ? tr("  (needed by another mod)") : QString()));
            }
            if (!warnings.isEmpty()) {
                lines.append(QString());
                lines.append(tr("Notes:"));
                lines += warnings;
            }
            // Confirmation must happen on the GUI thread: stash via context? Do it after.
            ctx.report(1, 1, lines.join(QLatin1Char('\n')));
            return true;
        },
        this);
    QString confirmText;
    connect(t, &Task::progressChanged, this, [&](qint64, qint64, const QString &m) { confirmText = m; });
    TaskProgressDialog prog(t, this);
    t->start();
    prog.exec();
    if (!prog.succeeded()) {
        t->deleteLater();
        if (!prog.errorText().isEmpty()) {
            QMessageBox::warning(this, tr("Couldn't resolve that mod"), prog.errorText());
        }
        return;
    }
    t->deleteLater();
    auto rc = QMessageBox::question(this, tr("Install “%1”?").arg(m_project.title),
                                    tr("This will add to “%1”:\n\n%2\n\nContinue?").arg(inst.name, confirmText));
    if (rc != QMessageBox::Yes) {
        return;
    }
    // Snapshot first (Embers), then install everything in one task.
    QString snapErr;
    const QString snap = m_embers ? m_embers->snapshot(inst.id, QStringLiteral("mod-install"), false, &snapErr)
                                  : QString();
    if (m_embers && snap.isEmpty()) {
        QMessageBox::warning(this, tr("Couldn't back up first"),
                             tr("Embers couldn't snapshot this profile (%1). Nothing was installed.").arg(snapErr));
        return;
    }
    auto *it = new LambdaTask(
        tr("Install %1").arg(m_project.title),
        [this, root, inst](Task::Context &ctx) {
            QStringList warnings2;
            QString err;
            const auto plan2 = m_modrinth->resolveInstallPlan({ root }, inst.versionId, inst.loaderType, ctx,
                                                              &warnings2, &err);
            if (plan2.isEmpty()) {
                if (!err.isEmpty()) {
                    ctx.fail(err);
                } else {
                    ctx.fail(tr("Nothing to install."));
                }
                return false;
            }
            int i = 0;
            for (const auto &u : plan2) {
                if (ctx.isCancelled()) {
                    ctx.fail(tr("Cancelled — use Undo last change to roll back."));
                    return false;
                }
                ctx.report(i++, plan2.size(), tr("Installing %1…").arg(u.project.title));
                if (!m_mods->installUnitBlocking(inst.id, u, 8, ctx)) {
                    return false;
                }
            }
            ctx.report(1, 1, tr("Done"));
            return true;
        },
        this);
    TaskProgressDialog iprog(it, this);
    connect(it, &Task::finished, this, [this, it, inst](bool ok2) {
        it->deleteLater();
        if (!ok2) {
            QMessageBox box(this);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(tr("Install had a problem"));
            box.setText(tr("%1\n\nEmbers took a snapshot first — “Undo last change” rolls this back.")
                            .arg(it->errorString()));
            box.addButton(QMessageBox::Ok);
            box.exec();
            return;
        }
        emit installedTo(inst.id);
        QMessageBox::information(this, tr("Installed"),
                                 tr("“%1” is ready in “%2”.").arg(m_project.title, inst.name));
    });
    it->start();
    iprog.exec();
}

// ---------------------------------------------------------------------------
// DiscoverPage
// ---------------------------------------------------------------------------

DiscoverPage::DiscoverPage(MojangApi *mojang, InstanceManager *instances, ModrinthApi *modrinth, ModManager *mods,
                           Embers *embers, const QString &dataDir, QWidget *parent)
    : QWidget(parent)
    , m_mojang(mojang)
    , m_instances(instances)
    , m_modrinth(modrinth)
    , m_mods(mods)
    , m_embers(embers)
    , m_dataDir(dataDir)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(10);

    auto *title = new QLabel(tr("Discover"), this);
    QFont tf = title->font();
    tf.setPointSize(20);
    tf.setBold(true);
    title->setFont(tf);
    outer->addWidget(title);
    auto *sub = new QLabel(tr("Mods, resource packs, shaders and more from Modrinth."), this);
    sub->setObjectName(QStringLiteral("secondary"));
    sub->setWordWrap(true);
    outer->addWidget(sub);

    m_banner = new QLabel(this);
    m_banner->setWordWrap(true);
    m_banner->setVisible(false);
    outer->addWidget(m_banner);

    // Filter row 1: search + type + sort.
    auto *row1 = new QHBoxLayout();
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("Search mods…"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::returnPressed, this, [this] { onSearch(true); });
    row1->addWidget(m_search, 1);
    m_type = new QComboBox(this);
    m_type->addItem(tr("Everything"), QStringLiteral("all"));
    m_type->addItem(tr("Mods"), QStringLiteral("mod"));
    m_type->addItem(tr("Modpacks"), QStringLiteral("modpack"));
    m_type->addItem(tr("Resource packs"), QStringLiteral("resourcepack"));
    m_type->addItem(tr("Shaders"), QStringLiteral("shader"));
    m_type->addItem(tr("Datapacks"), QStringLiteral("datapack"));
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { onSearch(true); });
    row1->addWidget(m_type);
    m_sort = new QComboBox(this);
    m_sort->addItem(tr("Most relevant"), QStringLiteral("relevance"));
    m_sort->addItem(tr("Most downloaded"), QStringLiteral("downloads"));
    m_sort->addItem(tr("Most followed"), QStringLiteral("follows"));
    m_sort->addItem(tr("Newest"), QStringLiteral("newest"));
    m_sort->addItem(tr("Recently updated"), QStringLiteral("updated"));
    connect(m_sort, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { onSearch(true); });
    row1->addWidget(m_sort);
    auto *goBtn = new QPushButton(IconProvider::instance().icon(QStringLiteral("search")), tr("Search"), this);
    connect(goBtn, &QPushButton::clicked, this, [this] { onSearch(true); });
    row1->addWidget(goBtn);
    outer->addLayout(row1);

    // Filter row 2: loader + version + category.
    auto *row2 = new QHBoxLayout();
    row2->addWidget(new QLabel(tr("Loader:"), this));
    m_loader = new QComboBox(this);
    m_loader->addItem(tr("Any"), QStringLiteral("any"));
    m_loader->addItem(QStringLiteral("Fabric"), QStringLiteral("fabric"));
    m_loader->addItem(QStringLiteral("Forge"), QStringLiteral("forge"));
    m_loader->addItem(QStringLiteral("Quilt"), QStringLiteral("quilt"));
    m_loader->addItem(QStringLiteral("NeoForge"), QStringLiteral("neoforge"));
    connect(m_loader, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { onSearch(true); });
    row2->addWidget(m_loader);
    row2->addWidget(new QLabel(tr("Game version:"), this));
    m_version = new QComboBox(this);
    m_version->addItem(tr("Any"), QStringLiteral("any"));
    {
        bool ok = false;
        const VersionManifest manifest = m_mojang ? m_mojang->cachedManifest(&ok) : VersionManifest{};
        if (ok) {
            int added = 0;
            for (const auto &e : manifest.versions) {
                if (e.type == QStringLiteral("release") && added < 30) {
                    m_version->addItem(e.id, e.id);
                    ++added;
                }
            }
        }
    }
    connect(m_version, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { onSearch(true); });
    row2->addWidget(m_version);
    row2->addWidget(new QLabel(tr("Category:"), this));
    m_category = new QComboBox(this);
    m_category->setEditable(true);
    m_category->addItem(tr("Any"), QStringLiteral("any"));
    for (const auto &c : { QStringLiteral("adventure"), QStringLiteral("decoration"), QStringLiteral("technology"),
                           QStringLiteral("magic"), QStringLiteral("optimization"), QStringLiteral("utility"),
                           QStringLiteral("worldgen"), QStringLiteral("equipment"), QStringLiteral("mobs") }) {
        m_category->addItem(c, c);
    }
    connect(m_category, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { onSearch(true); });
    row2->addWidget(m_category, 1);
    outer->addLayout(row2);

    m_list = new QListWidget(this);
    m_list->setSpacing(6);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &DiscoverPage::onOpenProject);
    outer->addWidget(m_list, 1);

    m_empty = new QLabel(this);
    m_empty->setPixmap(IconProvider::instance().pixmap(QStringLiteral("search"), 64, QColor(QStringLiteral("#8E8E93"))));
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setVisible(false);
    outer->addWidget(m_empty);

    auto *bottom = new QHBoxLayout();
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("secondary"));
    bottom->addWidget(m_status, 1);
    m_more = new QPushButton(tr("Load more"), this);
    connect(m_more, &QPushButton::clicked, this, &DiscoverPage::onLoadMore);
    m_more->setVisible(false);
    bottom->addWidget(m_more);
    outer->addLayout(bottom);

    m_nam = new QNetworkAccessManager(this);
    connect(m_nam, &QNetworkAccessManager::finished, this, &DiscoverPage::onIconReply);

    setOfflineBanner();
    connect(&NetworkStatus::instance(), &NetworkStatus::changed, this, &DiscoverPage::setOfflineBanner);
    // First paint: show something immediately (cached welcome search).
    onSearch(true);
}

void DiscoverPage::setOfflineBanner()
{
    const bool offline = NetworkStatus::instance().isEffectivelyOffline();
    m_banner->setVisible(offline);
    if (offline) {
        m_banner->setText(tr("You're offline — Discover shows cached results, which may be outdated. Mod installs "
                             "need a connection, but your installed mods keep working."));
    }
}

void DiscoverPage::onSearch(bool reset)
{
    if (m_searching) {
        return;
    }
    m_searching = true;
    if (reset) {
        m_current = ModrinthFilters{};
        m_total = 0;
        m_list->clear();
    }
    m_current.query = m_search->text();
    m_current.projectType = m_type->currentData().toString();
    if (m_current.projectType == QStringLiteral("all")) {
        m_current.projectType.clear();
    }
    m_current.loader = m_loader->currentData().toString();
    m_current.gameVersion = m_version->currentData().toString();
    m_current.category = m_category->currentData().toString();
    m_current.sort = m_sort->currentData().toString();
    m_current.limit = 20;
    if (!reset) {
        m_current.offset = m_list->count();
    } else {
        m_current.offset = 0;
    }
    const ModrinthFilters f = m_current;
    m_status->setText(tr("Searching…"));
    m_more->setVisible(false);
    auto page = std::make_shared<ModrinthSearchPage>();
    auto *t = new LambdaTask(
        tr("Search Modrinth"),
        [this, f, page](Task::Context &ctx) {
            bool cached = false;
            *page = m_modrinth->searchBlocking(f, ctx, &cached);
            page->offset = f.offset;
            page->limit = f.limit;
            if (page->hits.isEmpty()) {
                // Empty is fine (no results); real errors fail ctx inside.
                ctx.report(1, 1, cached ? tr("Offline — cached") : tr("Done"));
            }
            return true;
        },
        this);
    connect(t, &Task::finished, this, [this, t, f, reset, page](bool ok) {
        m_searching = false;
        const QString err = t->errorString();
        t->deleteLater();
        if (!ok) {
            // Offline with a previous search: show the cached view honestly.
            if (NetworkStatus::instance().isEffectivelyOffline()) {
                const ModrinthSearchPage cached = m_modrinth->lastSearchFromCache();
                if (!cached.hits.isEmpty()) {
                    addCards(cached, !reset);
                    m_status->setText(tr("Offline — showing %1 cached result(s).").arg(cached.hits.size()));
                    return;
                }
            }
            m_status->setText(err.isEmpty() ? tr("Search failed.") : err);
            return;
        }
        addCards(*page, !reset);
        m_total = page->totalHits;
        const int shown = m_list->count();
        m_status->setText(page->hits.isEmpty() && shown == 0 ? tr("Nothing found — try fewer filters.")
                          : tr("%1 shown%2.").arg(shown).arg(m_total > 0 ? tr(" of %1").arg(m_total) : QString()));
        m_more->setVisible(shown < m_total);
    });
    t->start();
}

void DiscoverPage::onLoadMore()
{
    onSearch(false);
}

QWidget *DiscoverPage::makeCard(const ModrinthSearchHit &hit)
{
    auto *card = new QWidget(m_list);
    auto *lay = new QHBoxLayout(card);
    lay->setContentsMargins(12, 10, 12, 10);
    auto *icon = new QLabel(card);
    icon->setFixedSize(48, 48);
    icon->setScaledContents(true);
    icon->setProperty("iconUrl", hit.iconUrl);
    lay->addWidget(icon);
    if (!hit.iconUrl.isEmpty()) {
        requestIcon(hit.iconUrl, icon);
    }
    auto *mid = new QVBoxLayout();
    auto *topRow = new QHBoxLayout();
    auto *name = new QLabel(hit.title, card);
    QFont nf = name->font();
    nf.setBold(true);
    nf.setPointSize(11);
    name->setFont(nf);
    topRow->addWidget(name);
    // Loader badges from the search index categories.
    QStringList badges;
    for (const auto &c : hit.categories) {
        const QString lc = c.toLower();
        if (lc == QStringLiteral("fabric") || lc == QStringLiteral("forge") || lc == QStringLiteral("quilt")
            || lc == QStringLiteral("neoforge")) {
            badges.append(c);
        }
    }
    if (!badges.isEmpty()) {
        auto *b = new QLabel(badges.join(QStringLiteral(" · ")), card);
        b->setObjectName(QStringLiteral("badge"));
        topRow->addWidget(b);
    }
    if (!hit.projectType.isEmpty()) {
        auto *tp = new QLabel(hit.projectType, card);
        tp->setObjectName(QStringLiteral("secondary"));
        topRow->addWidget(tp);
    }
    topRow->addStretch(1);
    mid->addLayout(topRow);
    auto *sum = new QLabel(hit.summary, card);
    sum->setObjectName(QStringLiteral("secondary"));
    sum->setWordWrap(true);
    mid->addWidget(sum);
    auto *meta = new QLabel(tr("by %1 · %2 downloads")
                                .arg(hit.author.isEmpty() ? tr("unknown") : hit.author,
                                     ModrinthMeta::prettyCount(hit.downloads)),
                            card);
    meta->setObjectName(QStringLiteral("secondary"));
    mid->addWidget(meta);
    lay->addLayout(mid, 1);
    return card;
}

void DiscoverPage::addCards(const ModrinthSearchPage &page, bool append)
{
    if (!append) {
        m_list->clear();
    }
    for (const auto &hit : page.hits) {
        auto *it = new QListWidgetItem(m_list);
        it->setData(Qt::UserRole, hit.projectId);
        it->setData(Qt::UserRole + 1, hit.slug);
        QWidget *card = makeCard(hit);
        it->setSizeHint(card->sizeHint());
        m_list->addItem(it);
        m_list->setItemWidget(it, card);
        // Keep the hit for the detail dialog.
        it->setData(Qt::UserRole + 2, hit.title);
        it->setData(Qt::UserRole + 3, hit.author);
        it->setData(Qt::UserRole + 4, hit.summary);
        it->setData(Qt::UserRole + 5, hit.iconUrl);
        it->setData(Qt::UserRole + 6, hit.projectType);
    }
    m_empty->setVisible(m_list->count() == 0);
}

void DiscoverPage::requestIcon(const QString &url, QLabel *label)
{
    // Disk cache first (offline-safe).
    const QByteArray key = QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString cached = QDir(m_modrinth->iconCacheDir()).filePath(QString::fromLatin1(key) + QStringLiteral(".bin"));
    if (QFile::exists(cached)) {
        QPixmap pm(cached);
        if (!pm.isNull()) {
            label->setPixmap(pm);
            return;
        }
    }
    if (NetworkStatus::instance().isEffectivelyOffline()) {
        return;
    }
    QNetworkRequest req{ QUrl(url) };
    QNetworkReply *rep = m_nam->get(req);
    rep->setParent(label); // cards outlive their fetch: clearing the list kills pending ones
    rep->setProperty("target", QVariant::fromValue<QObject *>(label));
    rep->setProperty("cachePath", cached);
}

void DiscoverPage::onIconReply(QNetworkReply *rep)
{
    rep->deleteLater();
    if (rep->error() != QNetworkReply::NoError) {
        return;
    }
    QObject *target = rep->property("target").value<QObject *>();
    auto *label = qobject_cast<QLabel *>(target);
    if (!label) {
        return;
    }
    const QByteArray data = rep->readAll();
    QPixmap pm;
    if (!pm.loadFromData(data) || pm.isNull()) {
        return;
    }
    const QString cachePath = rep->property("cachePath").toString();
    if (!cachePath.isEmpty()) {
        QDir().mkpath(QFileInfo(cachePath).absolutePath());
        QFile f(cachePath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(data);
        }
    }
    label->setPixmap(pm);
}

void DiscoverPage::onOpenProject(QListWidgetItem *it)
{
    if (!it) {
        return;
    }
    ModrinthSearchHit hit;
    hit.projectId = it->data(Qt::UserRole).toString();
    hit.slug = it->data(Qt::UserRole + 1).toString();
    hit.title = it->data(Qt::UserRole + 2).toString();
    hit.author = it->data(Qt::UserRole + 3).toString();
    hit.summary = it->data(Qt::UserRole + 4).toString();
    hit.iconUrl = it->data(Qt::UserRole + 5).toString();
    hit.projectType = it->data(Qt::UserRole + 6).toString();
    auto *dlg = new ProjectDialog(hit, m_mojang, m_instances, m_modrinth, m_mods, m_embers, m_dataDir, this);
    connect(dlg, &ProjectDialog::installModpackFile, this, &DiscoverPage::installModpackFile);
    connect(dlg, &ProjectDialog::installedTo, this, &DiscoverPage::installedTo);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

#include "DiscoverPage.moc"
