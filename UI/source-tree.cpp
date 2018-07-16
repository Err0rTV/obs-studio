#include "window-basic-main.hpp"
#include "obs-app.hpp"
#include "source-tree.hpp"
#include "qt-wrappers.hpp"
#include "visibility-checkbox.hpp"
#include "locked-checkbox.hpp"
#include "expand-checkbox.hpp"

#include <obs-frontend-api.h>
#include <obs.h>

#include <string>

#include <QLabel>
#include <QLineEdit>
#include <QSpacerItem>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>

#include <QStylePainter>
#include <QStyleOptionFocusRect>

static inline OBSScene GetCurrentScene()
{
	OBSBasic *main = reinterpret_cast<OBSBasic*>(App()->GetMainWindow());
	return main->GetCurrentScene();
}

/* ========================================================================= */

SourceTreeItem::SourceTreeItem(SourceTree *tree_, OBSSceneItem sceneitem_)
	: tree         (tree_),
	  sceneitem    (sceneitem_)
{
	setAttribute(Qt::WA_TranslucentBackground);

	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	const char *name = obs_source_get_name(source);

	vis = new VisibilityCheckBox();
	vis->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	vis->setMaximumSize(16, 16);
	vis->setChecked(obs_sceneitem_visible(sceneitem));

	lock = new LockedCheckBox();
	lock->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Maximum);
	lock->setMaximumSize(16, 16);
	lock->setChecked(obs_sceneitem_locked(sceneitem));

	label = new QLabel(QT_UTF8(name));
	label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
	label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
	label->setAttribute(Qt::WA_TranslucentBackground);

#ifdef __APPLE__
	vis->setAttribute(Qt::WA_LayoutUsesWidgetRect);
	lock->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif

	boxLayout = new QHBoxLayout();
	boxLayout->setContentsMargins(1, 1, 2, 1);
	boxLayout->setSpacing(1);
	boxLayout->addWidget(label);
	boxLayout->addWidget(vis);
	boxLayout->addWidget(lock);

	Update(false);

	setLayout(boxLayout);

	/* --------------------------------------------------------- */

	auto setItemVisible = [this] (bool checked)
	{
		SignalBlocker sourcesSignalBlocker(this);
		obs_sceneitem_set_visible(sceneitem, checked);
	};

	auto setItemLocked = [this] (bool checked)
	{
		SignalBlocker sourcesSignalBlocker(this);
		obs_sceneitem_set_locked(sceneitem, checked);
	};

	connect(vis, &QAbstractButton::clicked, setItemVisible);
	connect(lock, &QAbstractButton::clicked, setItemLocked);
}

void SourceTreeItem::DisconnectSignals()
{
	sceneRemoveSignal.Disconnect();
	itemRemoveSignal.Disconnect();
	visibleSignal.Disconnect();
	renameSignal.Disconnect();
	removeSignal.Disconnect();
}

void SourceTreeItem::ReconnectSignals()
{
	if (!sceneitem)
		return;

	DisconnectSignals();

	/* --------------------------------------------------------- */

	auto removeItem = [] (void *data, calldata_t *cd)
	{
		SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem*>(data);
		obs_sceneitem_t *curItem =
			(obs_sceneitem_t*)calldata_ptr(cd, "item");

		if (!curItem || curItem == this_->sceneitem) {
			this_->DisconnectSignals();
			this_->sceneitem = nullptr;
		}
	};

	auto itemVisible = [] (void *data, calldata_t *cd)
	{
		SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem*>(data);
		obs_sceneitem_t *curItem =
			(obs_sceneitem_t*)calldata_ptr(cd, "item");
		bool visible = calldata_bool(cd, "visible");

		if (curItem == this_->sceneitem)
			QMetaObject::invokeMethod(this_, "VisibilityChanged",
					Q_ARG(bool, visible));
	};

	obs_scene_t *scene = obs_sceneitem_get_scene(sceneitem);
	obs_source_t *sceneSource = obs_scene_get_source(scene);
	signal_handler_t *signal = obs_source_get_signal_handler(sceneSource);

	sceneRemoveSignal.Connect(signal, "remove", removeItem, this);
	itemRemoveSignal.Connect(signal, "item_remove", removeItem, this);
	visibleSignal.Connect(signal, "item_visible", itemVisible, this);

	/* --------------------------------------------------------- */

	auto renamed = [] (void *data, calldata_t *cd)
	{
		SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem*>(data);
		const char *name = calldata_string(cd, "new_name");

		QMetaObject::invokeMethod(this_, "Renamed",
				Q_ARG(QString, QT_UTF8(name)));
	};

	auto removeSource = [] (void *data, calldata_t *)
	{
		SourceTreeItem *this_ = reinterpret_cast<SourceTreeItem*>(data);
		this_->DisconnectSignals();
		this_->sceneitem = nullptr;
	};

	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	signal = obs_source_get_signal_handler(source);
	renameSignal.Connect(signal, "rename", renamed, this);
	removeSignal.Connect(signal, "remove", removeSource, this);
}

void SourceTreeItem::mouseDoubleClickEvent(QMouseEvent *event)
{
	QWidget::mouseDoubleClickEvent(event);

	if (expand) {
		expand->setChecked(!expand->isChecked());
	} else {
		obs_source_t *source = obs_sceneitem_get_source(sceneitem);
		OBSBasic *main =
			reinterpret_cast<OBSBasic*>(App()->GetMainWindow());
		if (source) {
			main->CreatePropertiesWindow(source);
		}
	}
}

void SourceTreeItem::EnterEditMode()
{
	setFocusPolicy(Qt::StrongFocus);
	boxLayout->removeWidget(label);
	editor = new QLineEdit(label->text());
	editor->installEventFilter(this);
	boxLayout->insertWidget(1, editor);
	setFocusProxy(editor);
}

void SourceTreeItem::ExitEditMode(bool save)
{
	if (!editor)
		return;

	OBSBasic *main = reinterpret_cast<OBSBasic*>(App()->GetMainWindow());
	OBSScene scene = main->GetCurrentScene();

	std::string newName = QT_TO_UTF8(editor->text());

	setFocusProxy(nullptr);
	boxLayout->removeWidget(editor);
	delete editor;
	editor = nullptr;
	setFocusPolicy(Qt::NoFocus);
	boxLayout->insertWidget(1, label);

	/* ----------------------------------------- */
	/* check for empty string                    */

	if (!save)
		return;

	if (newName.empty()) {
		OBSMessageBox::information(main,
			QTStr("NoNameEntered.Title"),
			QTStr("NoNameEntered.Text"));
		return;
	}

	/* ----------------------------------------- */
	/* Check for same name                       */

	obs_source_t *source = obs_sceneitem_get_source(sceneitem);
	if (newName == obs_source_get_name(source))
		return;

	/* ----------------------------------------- */
	/* check for existing source                 */

	bool exists = false;

	if (obs_sceneitem_is_group(sceneitem)) {
		exists = !!obs_scene_get_group(scene, newName.c_str());
	} else {
		obs_source_t *existingSource =
			obs_get_source_by_name(newName.c_str());
		obs_source_release(existingSource);
		exists = !!existingSource;
	}

	if (exists) {
		OBSMessageBox::information(main,
			QTStr("NameExists.Title"),
			QTStr("NameExists.Text"));
		return;
	}

	/* ----------------------------------------- */
	/* rename                                    */

	SignalBlocker sourcesSignalBlocker(this);
	obs_source_set_name(source, newName.c_str());
	label->setText(QT_UTF8(newName.c_str()));
}

bool SourceTreeItem::eventFilter(QObject *object, QEvent *event)
{
	if (editor != object)
		return false;

	if (event->type() == QEvent::KeyPress) {
		QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);

		switch (keyEvent->key()) {
		case Qt::Key_Escape:
			QMetaObject::invokeMethod(this, "ExitEditMode",
					Qt::QueuedConnection,
					Q_ARG(bool, false));
			return true;
		case Qt::Key_Tab:
		case Qt::Key_Backtab:
		case Qt::Key_Enter:
		case Qt::Key_Return:
			QMetaObject::invokeMethod(this, "ExitEditMode",
					Qt::QueuedConnection,
					Q_ARG(bool, true));
			return true;
		}
	} else if (event->type() == QEvent::FocusOut) {
		QMetaObject::invokeMethod(this, "ExitEditMode",
				Qt::QueuedConnection,
				Q_ARG(bool, false));
		return true;
	}

	return false;
}

void SourceTreeItem::VisibilityChanged(bool visible)
{
	vis->setChecked(visible);
}

void SourceTreeItem::Renamed(const QString &name)
{
	label->setText(name);
}

void SourceTreeItem::Update(bool force)
{
	OBSScene scene = GetCurrentScene();
	obs_scene_t *itemScene = obs_sceneitem_get_scene(sceneitem);

	Type newType;

	/* ------------------------------------------------- */
	/* if it's a group item, insert group checkbox       */

	if (obs_sceneitem_is_group(sceneitem)) {
		newType = Type::Group;

	/* ------------------------------------------------- */
	/* if it's a group sub-item                          */

	} else if (itemScene != scene) {
		newType = Type::SubItem;

	/* ------------------------------------------------- */
	/* if it's a regular item                            */

	} else {
		newType = Type::Item;
	}

	/* ------------------------------------------------- */

	if (!force && newType == type) {
		return;
	}

	/* ------------------------------------------------- */

	ReconnectSignals();

	if (spacer) {
		boxLayout->removeItem(spacer);
		delete spacer;
		spacer = nullptr;
	}

	if (type == Type::Group) {
		boxLayout->removeWidget(expand);
		expand->deleteLater();
		expand = nullptr;
	}

	type = newType;

	if (type == Type::SubItem) {
		spacer = new QSpacerItem(16, 1);
		boxLayout->insertItem(0, spacer);

	} else if (type == Type::Group) {
		expand = new SourceTreeSubItemCheckBox();
		expand->setSizePolicy(
				QSizePolicy::Maximum,
				QSizePolicy::Maximum);
		expand->setMaximumSize(10, 16);
		expand->setMinimumSize(10, 0);
#ifdef __APPLE__
		expand->setAttribute(Qt::WA_LayoutUsesWidgetRect);
#endif
		boxLayout->insertWidget(0, expand);

		obs_data_t *data = obs_sceneitem_get_private_settings(sceneitem);
		expand->blockSignals(true);
		expand->setChecked(obs_data_get_bool(data, "collapsed"));
		expand->blockSignals(false);
		obs_data_release(data);

		connect(expand, &QPushButton::toggled,
				this, &SourceTreeItem::ExpandClicked);

	} else {
		spacer = new QSpacerItem(3, 1);
		boxLayout->insertItem(0, spacer);
	}
}

void SourceTreeItem::ExpandClicked(bool checked)
{
	OBSData data = obs_sceneitem_get_private_settings(sceneitem);
	obs_data_release(data);

	obs_data_set_bool(data, "collapsed", checked);

	if (!checked)
		tree->GetStm()->ExpandGroup(sceneitem);
	else
		tree->GetStm()->CollapseGroup(sceneitem);
}

/* ========================================================================= */

void SourceTreeModel::OBSFrontendEvent(enum obs_frontend_event event, void *ptr)
{
	SourceTreeModel *stm = reinterpret_cast<SourceTreeModel *>(ptr);

	switch ((int)event) {
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		stm->SceneChanged();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		stm->Clear();
		break;
	}
}

void SourceTreeModel::Clear()
{
	beginResetModel();
	items.clear();
	endResetModel();

	hasGroups = false;
}

static bool enumItem(obs_scene_t*, obs_sceneitem_t *item, void *ptr)
{
	QVector<OBSSceneItem> &items =
		*reinterpret_cast<QVector<OBSSceneItem>*>(ptr);

	if (obs_sceneitem_is_group(item)) {
		obs_data_t *data = obs_sceneitem_get_private_settings(item);

		bool collapse = obs_data_get_bool(data, "collapsed");
		if (!collapse) {
			obs_scene_t *scene =
				obs_sceneitem_group_get_scene(item);

			obs_scene_enum_items(scene, enumItem, &items);
		}

		obs_data_release(data);
	}

	items.insert(0, item);
	return true;
}

void SourceTreeModel::SceneChanged()
{
	OBSScene scene = GetCurrentScene();

	beginResetModel();
	items.clear();
	obs_scene_enum_items(scene, enumItem, &items);
	endResetModel();

	UpdateGroupState(false);
	st->ResetWidgets();

	for (int i = 0; i < items.count(); i++) {
		bool select = obs_sceneitem_selected(items[i]);
		QModelIndex index = createIndex(i, 0);

		st->selectionModel()->select(index, select
				? QItemSelectionModel::Select
				: QItemSelectionModel::Deselect);
	}
}

/* moves a scene item index (blame linux distros for using older Qt builds) */
static inline void MoveItem(QVector<OBSSceneItem> &items, int oldIdx, int newIdx)
{
	OBSSceneItem item = items[oldIdx];
	items.remove(oldIdx);
	items.insert(newIdx, item);
}

/* reorders list optimally with model reorder funcs */
void SourceTreeModel::ReorderItems()
{
	OBSScene scene = GetCurrentScene();

	QVector<OBSSceneItem> newitems;
	obs_scene_enum_items(scene, enumItem, &newitems);

	/* if item list has changed size, do full reset */
	if (newitems.count() != items.count()) {
		SceneChanged();
		return;
	}

	for (;;) {
		int idx1Old = 0;
		int idx1New = 0;
		int count;
		int i;

		/* find first starting changed item index */
		for (i = 0; i < newitems.count(); i++) {
			obs_sceneitem_t *oldItem = items[i];
			obs_sceneitem_t *newItem = newitems[i];
			if (oldItem != newItem) {
				idx1Old = i;
				break;
			}
		}

		/* if everything is the same, break */
		if (i == newitems.count()) {
			break;
		}

		/* find new starting index */
		for (i = idx1Old + 1; i < newitems.count(); i++) {
			obs_sceneitem_t *oldItem = items[idx1Old];
			obs_sceneitem_t *newItem = newitems[i];

			if (oldItem == newItem) {
				idx1New = i;
				break;
			}
		}

		/* if item could not be found, do full reset */
		if (i == newitems.count()) {
			SceneChanged();
			return;
		}

		/* get move count */
		for (count = 1; (idx1New + count) < newitems.count(); count++) {
			int oldIdx = idx1Old + count;
			int newIdx = idx1New + count;

			obs_sceneitem_t *oldItem = items[oldIdx];
			obs_sceneitem_t *newItem = newitems[newIdx];

			if (oldItem != newItem) {
				break;
			}
		}

		/* move items */
		beginMoveRows(QModelIndex(), idx1Old, idx1Old + count - 1,
		              QModelIndex(), idx1New + count);
		for (i = 0; i < count; i++) {
			int to = idx1New + count;
			if (to > idx1Old)
				to--;
			MoveItem(items, idx1Old, to);
		}
		endMoveRows();
	}
}

void SourceTreeModel::Add(obs_sceneitem_t *item)
{
	beginInsertRows(QModelIndex(), 0, 0);
	items.insert(0, item);
	endInsertRows();

	st->UpdateWidget(createIndex(0, 0, nullptr), item);
}

void SourceTreeModel::Remove(obs_sceneitem_t *item)
{
	int idx = -1;
	for (int i = 0; i < items.count(); i++) {
		if (items[i] == item) {
			idx = i;
			break;
		}
	}

	if (idx == -1)
		return;

	int startIdx = idx;
	int endIdx = idx;

	bool is_group = obs_sceneitem_is_group(item);
	if (is_group) {
		obs_scene_t *scene = obs_sceneitem_group_get_scene(item);

		for (int i = endIdx + 1; i < items.count(); i++) {
			obs_sceneitem_t *subitem = items[i];
			obs_scene_t *subscene =
				obs_sceneitem_get_scene(subitem);

			if (subscene == scene)
				endIdx = i;
			else
				break;
		}
	}

	beginRemoveRows(QModelIndex(), startIdx, endIdx);
	items.remove(idx, endIdx - startIdx + 1);
	endRemoveRows();

	if (is_group)
		UpdateGroupState(true);
}

OBSSceneItem SourceTreeModel::Get(int idx)
{
	if (idx == -1 || idx >= items.count())
		return OBSSceneItem();
	return items[idx];
}

SourceTreeModel::SourceTreeModel(SourceTree *st_)
	: QAbstractListModel (st_),
	  st                 (st_)
{
	obs_frontend_add_event_callback(OBSFrontendEvent, this);
}

SourceTreeModel::~SourceTreeModel()
{
	obs_frontend_remove_event_callback(OBSFrontendEvent, this);
}

int SourceTreeModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : items.count();
}

QVariant SourceTreeModel::data(const QModelIndex &, int) const
{
	return QVariant();
}

Qt::ItemFlags SourceTreeModel::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return QAbstractListModel::flags(index) | Qt::ItemIsDropEnabled;

	obs_sceneitem_t *item = items[index.row()];
	bool is_group = obs_sceneitem_is_group(item);

	return QAbstractListModel::flags(index) |
	       Qt::ItemIsEditable |
	       Qt::ItemIsDragEnabled |
	       (is_group ? Qt::ItemIsDropEnabled : Qt::NoItemFlags);
}

Qt::DropActions SourceTreeModel::supportedDropActions() const
{
	return QAbstractItemModel::supportedDropActions() | Qt::MoveAction;
}

QString SourceTreeModel::GetNewGroupName()
{
	OBSScene scene = GetCurrentScene();
	QString name = QTStr("Group");

	int i = 2;
	for (;;) {
		obs_sceneitem_t *group = obs_scene_get_group(scene,
				QT_TO_UTF8(name));
		if (!group)
			break;
		name = QTStr("Basic.Main.Group").arg(QString::number(i++));
	}

	return name;
}

void SourceTreeModel::AddGroup()
{
	QString name = GetNewGroupName();
	obs_sceneitem_t *group = obs_scene_add_group(GetCurrentScene(),
			QT_TO_UTF8(name));
	if (!group)
		return;

	beginInsertRows(QModelIndex(), 0, 0);
	items.insert(0, group);
	endInsertRows();

	st->UpdateWidget(createIndex(0, 0, nullptr), group);
	UpdateGroupState(true);

	QMetaObject::invokeMethod(st, "Edit", Qt::QueuedConnection,
			Q_ARG(int, 0));
}

void SourceTreeModel::GroupSelectedItems(QModelIndexList &indices)
{
	if (indices.count() == 0)
		return;

	OBSScene scene = GetCurrentScene();
	QString name = GetNewGroupName();

	QVector<obs_sceneitem_t *> item_order;

	for (int i = indices.count() - 1; i >= 0; i--) {
		obs_sceneitem_t *item = items[indices[i].row()];
		item_order << item;
	}

	obs_sceneitem_t *item = obs_scene_insert_group(
			scene, QT_TO_UTF8(name),
			item_order.data(), item_order.size());
	if (!item) {
		return;
	}

	for (obs_sceneitem_t *item : item_order)
		obs_sceneitem_select(item, false);

	int newIdx = indices[0].row();

	beginInsertRows(QModelIndex(), newIdx, newIdx);
	items.insert(newIdx, item);
	endInsertRows();

	for (int i = 0; i < indices.size(); i++) {
		int fromIdx = indices[i].row() + 1;
		int toIdx = newIdx + i + 1;
		if (fromIdx != toIdx) {
			beginMoveRows(QModelIndex(), fromIdx, fromIdx,
			              QModelIndex(), toIdx);
			MoveItem(items, fromIdx, toIdx);
			endMoveRows();
		}
	}

	hasGroups = true;
	st->UpdateWidgets(true);

	obs_sceneitem_select(item, true);

	QMetaObject::invokeMethod(st, "Edit", Qt::QueuedConnection,
			Q_ARG(int, newIdx));
}

void SourceTreeModel::UngroupSelectedGroups(QModelIndexList &indices)
{
	if (indices.count() == 0)
		return;

	for (int i = indices.count() - 1; i >= 0; i--) {
		obs_sceneitem_t *item = items[indices[i].row()];
		obs_sceneitem_group_ungroup(item);
	}

	SceneChanged();
}

void SourceTreeModel::ExpandGroup(obs_sceneitem_t *item)
{
	int itemIdx = items.indexOf(item);
	if (itemIdx == -1)
		return;

	itemIdx++;

	obs_scene_t *scene = obs_sceneitem_group_get_scene(item);

	QVector<OBSSceneItem> subItems;
	obs_scene_enum_items(scene, enumItem, &subItems);

	if (!subItems.size())
		return;

	beginInsertRows(QModelIndex(), itemIdx, itemIdx + subItems.size() - 1);
	for (int i = 0; i < subItems.size(); i++)
		items.insert(i + itemIdx, subItems[i]);
	endInsertRows();

	st->UpdateWidgets();
}

void SourceTreeModel::CollapseGroup(obs_sceneitem_t *item)
{
	int startIdx = -1;
	int endIdx = -1;

	obs_scene_t *scene = obs_sceneitem_group_get_scene(item);

	for (int i = 0; i < items.size(); i++) {
		obs_scene_t *itemScene = obs_sceneitem_get_scene(items[i]);

		if (itemScene == scene) {
			if (startIdx == -1)
				startIdx = i;
			endIdx = i;
		}
	}

	if (startIdx == -1)
		return;

	beginRemoveRows(QModelIndex(), startIdx, endIdx);
	items.remove(startIdx, endIdx - startIdx + 1);
	endRemoveRows();
}

void SourceTreeModel::UpdateGroupState(bool update)
{
	bool nowHasGroups = false;
	for (auto &item : items) {
		if (obs_sceneitem_is_group(item)) {
			nowHasGroups = true;
			break;
		}
	}

	if (nowHasGroups != hasGroups) {
		hasGroups = nowHasGroups;
		if (update) {
			st->UpdateWidgets(true);
		}
	}
}

/* ========================================================================= */

SourceTree::SourceTree(QWidget *parent_) : QListView(parent_)
{
	SourceTreeModel *stm_ = new SourceTreeModel(this);
	setModel(stm_);
}

void SourceTree::ResetWidgets()
{
	OBSScene scene = GetCurrentScene();

	SourceTreeModel *stm = GetStm();
	stm->UpdateGroupState(false);

	for (int i = 0; i < stm->items.count(); i++) {
		QModelIndex index = stm->createIndex(i, 0, nullptr);
		setIndexWidget(index, new SourceTreeItem(this, stm->items[i]));
	}
}

void SourceTree::UpdateWidget(const QModelIndex &idx, obs_sceneitem_t *item)
{
	setIndexWidget(idx, new SourceTreeItem(this, item));
}

void SourceTree::UpdateWidgets(bool force)
{
	SourceTreeModel *stm = GetStm();

	for (int i = 0; i < stm->items.size(); i++) {
		obs_sceneitem_t *item = stm->items[i];
		SourceTreeItem *widget = GetItemWidget(i);

		if (!widget) {
			UpdateWidget(stm->createIndex(i, 0), item);
		} else {
			widget->Update(force);
		}
	}
}

void SourceTree::SelectItem(obs_sceneitem_t *sceneitem, bool select)
{
	SourceTreeModel *stm = GetStm();
	int i = 0;

	for (; i < stm->items.count(); i++) {
		if (stm->items[i] == sceneitem)
			break;
	}

	if (i == stm->items.count())
		return;

	QModelIndex index = stm->createIndex(i, 0);
	if (index.isValid())
		selectionModel()->select(index, select
				? QItemSelectionModel::Select
				: QItemSelectionModel::Deselect);
}

Q_DECLARE_METATYPE(OBSSceneItem);

void SourceTree::mouseDoubleClickEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton)
		QListView::mouseDoubleClickEvent(event);
}

void SourceTree::dropEvent(QDropEvent *event)
{
	if (event->source() != this) {
		QListView::dropEvent(event);
		return;
	}

	OBSScene scene = GetCurrentScene();
	SourceTreeModel *stm = GetStm();
	auto &items = stm->items;
	QModelIndexList indices = selectedIndexes();

	DropIndicatorPosition indicator = dropIndicatorPosition();
	int row = indexAt(event->pos()).row();
	bool emptyDrop = row == -1;

	if (emptyDrop) {
		if (!items.size()) {
			QListView::dropEvent(event);
			return;
		}

		row = items.size() - 1;
		indicator = QAbstractItemView::BelowItem;
	}

	/* --------------------------------------- */
	/* store destination group if moving to a  */
	/* group                                   */

	obs_sceneitem_t *dropItem = items[row]; /* item being dropped on */
	bool itemIsGroup = obs_sceneitem_is_group(dropItem);

	obs_sceneitem_t *dropGroup = itemIsGroup
		? dropItem
		: obs_sceneitem_get_group(scene, dropItem);

	/* not a group if moving above the group */
	if (indicator == QAbstractItemView::AboveItem && itemIsGroup)
		dropGroup = nullptr;
	if (emptyDrop)
		dropGroup = nullptr;

	/* --------------------------------------- */
	/* remember to remove list items if        */
	/* dropping on collapsed group             */

	bool dropOnCollapsed = false;
	if (dropGroup) {
		obs_data_t *data = obs_sceneitem_get_private_settings(dropGroup);
		dropOnCollapsed = obs_data_get_bool(data, "collapsed");
		obs_data_release(data);
	}

	if (indicator == QAbstractItemView::BelowItem ||
	    indicator == QAbstractItemView::OnItem)
		row++;

	if (row < 0 || row > stm->items.count()) {
		QListView::dropEvent(event);
		return;
	}

	/* --------------------------------------- */
	/* determine if any base group is selected */

	bool hasGroups = false;
	for (int i = 0; i < indices.size(); i++) {
		obs_sceneitem_t *item = items[indices[i].row()];
		if (obs_sceneitem_is_group(item)) {
			hasGroups = true;
			break;
		}
	}

	/* --------------------------------------- */
	/* if dropping a group, detect if it's     */
	/* below another group                     */

	obs_sceneitem_t *itemBelow = row == stm->items.count()
		? nullptr
		: stm->items[row];
	if (hasGroups) {
		if (!itemBelow ||
		    obs_sceneitem_get_group(scene, itemBelow) != dropGroup) {
			indicator = QAbstractItemView::BelowItem;
			dropGroup = nullptr;
			dropOnCollapsed = false;
		}
	}

	/* --------------------------------------- */
	/* if dropping groups on other groups,     */
	/* disregard as invalid drag/drop          */

	if (dropGroup && hasGroups) {
		QListView::dropEvent(event);
		return;
	}

	/* --------------------------------------- */
	/* if selection includes base group items, */
	/* include all group sub-items and treat   */
	/* them all as one                         */

	if (hasGroups) {
		/* remove sub-items if selected */
		for (int i = indices.size() - 1; i >= 0; i--) {
			obs_sceneitem_t *item = items[indices[i].row()];
			obs_scene_t *itemScene = obs_sceneitem_get_scene(item);

			if (itemScene != scene) {
				indices.removeAt(i);
			}
		}

		/* add all sub-items of selected groups */
		for (int i = indices.size() - 1; i >= 0; i--) {
			obs_sceneitem_t *item = items[indices[i].row()];

			if (obs_sceneitem_is_group(item)) {
				for (int j = items.size() - 1; j >= 0; j--) {
					obs_sceneitem_t *subitem = items[j];
					obs_sceneitem_t *subitemGroup =
						obs_sceneitem_get_group(scene,
								subitem);

					if (subitemGroup == item) {
						QModelIndex idx =
							stm->createIndex(j, 0);
						indices.insert(i + 1, idx);
					}
				}
			}
		}
	}

	/* --------------------------------------- */
	/* build persistent indices                */

	QList<QPersistentModelIndex> persistentIndices;
	persistentIndices.reserve(indices.count());
	for (QModelIndex &index : indices)
		persistentIndices.append(index);
	std::sort(persistentIndices.begin(), persistentIndices.end());

	/* --------------------------------------- */
	/* move all items to destination index     */

	int r = row;
	for (auto &persistentIdx : persistentIndices) {
		int from = persistentIdx.row();
		int to = r;
		int itemTo = to;

		if (itemTo > from)
			itemTo--;

		if (itemTo != from) {
			stm->beginMoveRows(QModelIndex(), from, from,
			                   QModelIndex(), to);
			MoveItem(items, from, itemTo);
			stm->endMoveRows();
		}

		r = persistentIdx.row() + 1;
	}

	std::sort(persistentIndices.begin(), persistentIndices.end());
	int firstIdx = persistentIndices.front().row();
	int lastIdx = persistentIndices.back().row();

	/* --------------------------------------- */
	/* reorder scene items in back-end         */

	QVector<struct obs_sceneitem_order_info> orderList;
	obs_sceneitem_t *lastGroup = nullptr;
	int insertCollapsedIdx = 0;

	auto insertCollapsed = [&] (obs_sceneitem_t *item)
	{
		struct obs_sceneitem_order_info info;
		info.group = lastGroup;
		info.item = item;

		orderList.insert(insertCollapsedIdx++, info);
	};

	using insertCollapsed_t = decltype(insertCollapsed);

	auto preInsertCollapsed = [] (obs_scene_t *, obs_sceneitem_t *item,
			void *param)
	{
		(*reinterpret_cast<insertCollapsed_t *>(param))(item);
		return true;
	};

	auto insertLastGroup = [&] ()
	{
		obs_data_t *data = obs_sceneitem_get_private_settings(lastGroup);
		bool collapsed = obs_data_get_bool(data, "collapsed");
		obs_data_release(data);

		if (collapsed) {
			insertCollapsedIdx = 0;
			obs_sceneitem_group_enum_items(
					lastGroup,
					preInsertCollapsed,
					&insertCollapsed);
		}

		struct obs_sceneitem_order_info info;
		info.group = nullptr;
		info.item = lastGroup;
		orderList.insert(0, info);
	};

	auto updateScene = [&] ()
	{
		struct obs_sceneitem_order_info info;

		for (int i = 0; i < items.size(); i++) {
			obs_sceneitem_t *item = items[i];
			obs_sceneitem_t *group;

			if (obs_sceneitem_is_group(item)) {
				if (lastGroup) {
					insertLastGroup();
				}
				lastGroup = item;
				continue;
			}

			if (!hasGroups && i >= firstIdx && i <= lastIdx)
				group = dropGroup;
			else
				group = obs_sceneitem_get_group(scene, item);

			if (lastGroup && lastGroup != group) {
				insertLastGroup();
			}

			lastGroup = group;

			info.group = group;
			info.item = item;
			orderList.insert(0, info);
		}

		if (lastGroup) {
			insertLastGroup();
		}

		obs_scene_reorder_items2(scene,
				orderList.data(), orderList.size());
	};

	using updateScene_t = decltype(updateScene);

	auto preUpdateScene = [] (void *data, obs_scene_t *)
	{
		(*reinterpret_cast<updateScene_t *>(data))();
	};

	ignoreReorder = true;
	obs_scene_atomic_update(scene, preUpdateScene, &updateScene);
	ignoreReorder = false;

	/* --------------------------------------- */
	/* remove items if dropped in to collapsed */
	/* group                                   */

	if (dropOnCollapsed) {
		stm->beginRemoveRows(QModelIndex(), firstIdx, lastIdx);
		items.remove(firstIdx, lastIdx - firstIdx + 1);
		stm->endRemoveRows();
	}

	/* --------------------------------------- */
	/* update widgets and accept event         */

	UpdateWidgets(true);

	event->accept();
	event->setDropAction(Qt::CopyAction);

	QListView::dropEvent(event);
}

void SourceTree::selectionChanged(
		const QItemSelection &selected,
		const QItemSelection &deselected)
{
	{
		SignalBlocker sourcesSignalBlocker(this);
		SourceTreeModel *stm = GetStm();

		QModelIndexList selectedIdxs = selected.indexes();
		QModelIndexList deselectedIdxs = deselected.indexes();

		for (int i = 0; i < selectedIdxs.count(); i++) {
			int idx = selectedIdxs[i].row();
			obs_sceneitem_select(stm->items[idx], true);
		}

		for (int i = 0; i < deselectedIdxs.count(); i++) {
			int idx = deselectedIdxs[i].row();
			obs_sceneitem_select(stm->items[idx], false);
		}
	}
	QListView::selectionChanged(selected, deselected);
}

void SourceTree::Edit(int row)
{
	SourceTreeModel *stm = GetStm();
	if (row < 0 || row >= stm->items.count())
		return;

	QWidget *widget = indexWidget(stm->createIndex(row, 0));
	SourceTreeItem *itemWidget = reinterpret_cast<SourceTreeItem *>(widget);
	itemWidget->EnterEditMode();
	edit(stm->createIndex(row, 0));
}

bool SourceTree::MultipleBaseSelected() const
{
	SourceTreeModel *stm = GetStm();
	QModelIndexList selectedIndices = selectedIndexes();

	OBSScene scene = GetCurrentScene();

	if (selectedIndices.size() < 1) {
		return false;
	}

	for (auto &idx : selectedIndices) {
		obs_sceneitem_t *item = stm->items[idx.row()];
		if (obs_sceneitem_is_group(item)) {
			return false;
		}

		obs_scene *itemScene = obs_sceneitem_get_scene(item);
		if (itemScene != scene) {
			return false;
		}
	}

	return true;
}

bool SourceTree::GroupsSelected() const
{
	SourceTreeModel *stm = GetStm();
	QModelIndexList selectedIndices = selectedIndexes();

	OBSScene scene = GetCurrentScene();

	if (selectedIndices.size() < 1) {
		return false;
	}

	for (auto &idx : selectedIndices) {
		obs_sceneitem_t *item = stm->items[idx.row()];
		if (!obs_sceneitem_is_group(item)) {
			return false;
		}
	}

	return true;
}

bool SourceTree::GroupedItemsSelected() const
{
	SourceTreeModel *stm = GetStm();
	QModelIndexList selectedIndices = selectedIndexes();
	OBSScene scene = GetCurrentScene();

	if (!selectedIndices.size()) {
		return false;
	}

	for (auto &idx : selectedIndices) {
		obs_sceneitem_t *item = stm->items[idx.row()];
		obs_scene *itemScene = obs_sceneitem_get_scene(item);

		if (itemScene != scene) {
			return true;
		}
	}

	return false;
}

void SourceTree::GroupSelectedItems()
{
	QModelIndexList indices = selectedIndexes();
	std::sort(indices.begin(), indices.end());
	GetStm()->GroupSelectedItems(indices);
}

void SourceTree::UngroupSelectedGroups()
{
	QModelIndexList indices = selectedIndexes();
	GetStm()->UngroupSelectedGroups(indices);
}

void SourceTree::AddGroup()
{
	GetStm()->AddGroup();
}